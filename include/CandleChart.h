#pragma once
#include "ClockCore.h"

namespace btc {
// Rational UTC boundaries avoid rounding range/21 and avoid rebucketing old
// candles on every tick. The rightmost column is the currently forming candle.
inline uint32_t candleBucket(uint64_t stampMs,uint8_t window){
  return window<5?uint32_t(stampMs*21/(uint64_t(config::kWindowSeconds[window])*1000)):0;
}
inline uint64_t candleStart(uint32_t bucket,uint8_t window){
  return (uint64_t(bucket)*config::kWindowSeconds[window]*1000+20)/21;
}
struct Candle {
  uint32_t bucket=0,openAt=0,closeAt=0; // Millisecond offsets from bucket start.
  uint32_t open=0,high=0,low=0,close=0; // USD cents; independent of Nixie rounding.
};
static_assert(sizeof(Candle)==28,"Bounded OHLC storage");
inline bool validCandle(const Candle& c,uint8_t w){
  return w<5&&c.bucket&&c.low&&c.low<=std::min(c.open,c.close)&&c.high>=std::max(c.open,c.close)&&
    c.high>=c.low&&c.openAt<=c.closeAt&&c.closeAt<candleStart(c.bucket+1,w)-candleStart(c.bucket,w);
}
struct CandleSeries {
  Candle bars[21]={};
  bool add(uint64_t stampMs,double price,uint8_t window,uint64_t nowMs,bool tieOpen=false,bool tieClose=true){
    if(window>=5||nowMs<uint64_t(config::kMinimumEpoch)*1000||stampMs>nowMs||!validPrice(price)||price>UINT32_MAX/100.0)return false;
    uint32_t current=candleBucket(nowMs,window),bucket=candleBucket(stampMs,window);
    if(bucket+20<current||bucket>current)return false;
    uint32_t cents=uint32_t(std::llround(price*100)),offset=uint32_t(stampMs-candleStart(bucket,window));
    if(!cents)return false;
    Candle& c=bars[bucket%21];Candle previous=c;
    if(c.bucket!=bucket)c={};
    if(!c.bucket){c.bucket=bucket;c.openAt=c.closeAt=offset;c.open=c.high=c.low=c.close=cents;}
    else{
      c.high=std::max(c.high,cents);c.low=std::min(c.low,cents);
      if(offset<c.openAt||(offset==c.openAt&&tieOpen)){c.openAt=offset;c.open=cents;}
      if(offset>c.closeAt||(offset==c.closeAt&&tieClose)){c.closeAt=offset;c.close=cents;}
    }
    return memcmp(&previous,&c,sizeof(c))!=0;
  }
  bool trim(uint8_t window,uint64_t nowMs){
    if(window>=5||nowMs<uint64_t(config::kMinimumEpoch)*1000)return false;
    uint32_t current=candleBucket(nowMs,window);bool changed=false;
    for(auto& c:bars)if(c.bucket&&(c.bucket+20<current||c.bucket>current)){c={};changed=true;}
    return changed;
  }
  bool merge(const CandleSeries& incoming,uint8_t window,uint64_t nowMs){
    bool changed=trim(window,nowMs);uint32_t current=candleBucket(nowMs,window);
    for(const auto& c:incoming.bars){
      if(!validCandle(c,window)||c.bucket+20<current||c.bucket>current)continue;
      auto& to=bars[c.bucket%21];Candle previous=to;
      if(to.bucket!=c.bucket)to=c;
      else{
        to.high=std::max(to.high,c.high);to.low=std::min(to.low,c.low);
        if(c.openAt<to.openAt){to.openAt=c.openAt;to.open=c.open;}
        if(c.closeAt>to.closeAt){to.closeAt=c.closeAt;to.close=c.close;}
      }
      changed|=memcmp(&previous,&to,sizeof(to))!=0;
    }
    return changed;
  }
  unsigned count(uint8_t window,uint64_t nowMs)const{
    if(window>=5)return 0;
    unsigned n=0;uint32_t current=candleBucket(nowMs,window);
    for(const auto& c:bars)n+=c.bucket&&c.bucket+20>=current&&c.bucket<=current;
    return n;
  }
  bool extent(uint8_t window,uint64_t nowMs,double& low,double& high)const{
    low=1e12;high=0;uint32_t current=candleBucket(nowMs,window);
    for(const auto& c:bars)if(c.bucket&&c.bucket+20>=current&&c.bucket<=current){low=std::min(low,c.low/100.0);high=std::max(high,c.high/100.0);}
    return high>0;
  }
};
struct CandleScale {
  double low=0,step=0,smoothedSpan=0;
  uint32_t lastEpoch=0,lastPan=0,lastResize=0;
  bool valid()const{return std::isfinite(low)&&std::abs(low)<=1e10&&std::isfinite(step)&&step>=0.01&&step<=1e8&&std::isfinite(smoothedSpan)&&smoothedSpan>=0.12&&smoothedSpan<=1e10;}
  void resetTimers(){lastEpoch=lastPan=lastResize=0;}
  void fit(double bottom,double top){
    double span=std::max(2.0,(top-bottom)*1.25);
    // Reserve one row at each edge, with a stable whole-row price grid.
    step=std::max(0.01,std::ceil(span/10*100)/100);
    low=std::floor(((bottom+top)/2-step*6)/step)*step;
  }
  bool update(const CandleSeries& series,uint8_t window,uint64_t nowMs){
    double bottom,top;if(!series.extent(window,nowMs,bottom,top))return false;
    double beforeLow=low,beforeStep=step,beforeSmooth=smoothedSpan;
    uint32_t epoch=uint32_t(nowMs/1000);
    double demand=std::max(2.0,(top-bottom)*1.25);
    if(!valid()){fit(bottom,top);smoothedSpan=step*12;lastEpoch=lastPan=lastResize=epoch;return true;}
    // Only observed online time contributes; a reboot resets these timers.
    uint32_t dt=lastEpoch&&epoch>=lastEpoch?std::min<uint32_t>(300,epoch-lastEpoch):0;
    smoothedSpan+=(demand-smoothedSpan)*(-std::expm1(-double(dt)/1800.0));
    lastEpoch=epoch;
    double span=step*12;
    // A move more than two rows outside the view, or an extent 50% larger
    // than the current view, is a spike. Expand promptly, never shrink here.
    if(top>low+span+2*step||bottom<low-2*step||top-bottom>span*1.5){
      double oldStep=step;fit(std::min(bottom,low),std::max(top,low+span));step=std::max(step,oldStep);
      low=std::floor(((bottom+top)/2-step*6)/step)*step;lastPan=lastResize=epoch;
    }else{
      if(!lastPan)lastPan=epoch;
      if(epoch>=lastPan&&epoch-lastPan>=5){
        double center=(bottom+top)/2,viewCenter=low+step*6;
        if(center>viewCenter+step)low+=step;
        else if(center<viewCenter-step)low-=step;
        lastPan=epoch; // At most one whole row per five seconds.
      }
      if(!lastResize)lastResize=epoch;
      if(epoch>=lastResize&&epoch-lastResize>=60){
        // Quantized 5% steps after 15% hysteresis; 30-minute EMA prevents
        // ordinary per-tick highs/lows from continually changing candle height.
        double next=step;
        if(smoothedSpan>span*1.15)next=step*1.05;
        else if(smoothedSpan<span*0.85)next=std::max(0.01,step*0.95);
        if(next!=step){double center=low+step*6;step=next;low=center-step*6;}
        lastResize=epoch;
      }
    }
    return beforeLow!=low||beforeStep!=step||beforeSmooth!=smoothedSpan;
  }
  uint8_t row(uint32_t cents)const{double position=(cents/100.0-low)/step;if(position<=0)return 12;if(position>=12)return 0;return uint8_t(12-std::lround(position));}
};
struct CandleFrame {
  uint16_t bodies[21]={},wicks[21]={};uint32_t valid=0;
  Plot plot(bool showWicks)const{
    Plot p={};p.valid=valid;
    for(unsigned col=0;col<21;++col){p.connectors[col]=bodies[col]|(showWicks?wicks[col]:0);p.rows[col]=0;
      if(p.connectors[col])p.rows[col]=uint8_t(__builtin_ctz(unsigned(p.connectors[col])));}
    return p;
  }
};
inline uint16_t candleMask(uint8_t a,uint8_t b){
  uint16_t mask=0;for(unsigned row=std::min(a,b);row<=std::max(a,b);++row)mask|=uint16_t(1U<<row);return mask;
}
inline CandleFrame makeCandles(const CandleSeries& series,const CandleScale& scale,uint8_t window,uint64_t nowMs){
  CandleFrame frame;if(window>=5||!scale.valid())return frame;
  uint32_t current=candleBucket(nowMs,window);
  for(const auto& c:series.bars)if(c.bucket&&c.bucket+20>=current&&c.bucket<=current){
    unsigned col=c.bucket+20-current;frame.valid|=1U<<col;
    frame.bodies[col]=candleMask(scale.row(c.open),scale.row(c.close));
    frame.wicks[col]=candleMask(scale.row(c.high),scale.row(c.low));
  }
  return frame;
}
}
