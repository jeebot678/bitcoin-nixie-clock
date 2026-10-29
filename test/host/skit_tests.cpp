#include "SkitChart.h"
#include <cassert>
int main(){
  assert(skit::kRefreshMs==500);
  for(unsigned t=0;t<15000;t+=500)assert(skit::digits(t)>=80875&&skit::digits(t)<=81125);
  uint32_t previous=skit::digits(15000);
  for(unsigned t=15500;t<=35000;t+=500){auto d=skit::digits(t);assert(d>=previous&&d<=999999);previous=d;}
  assert(skit::price(35000)>1000000);
  assert(skit::digits(35000)==999999&&skit::digits(44500)==999999);
  previous=skit::digits(45000);
  for(unsigned t=45500;t<=48000;t+=500){auto d=skit::digits(t);assert(d<=previous);previous=d;}
  assert(skit::digits(48000)==1&&skit::digits(59500)==1);
  assert(skit::kCycleMs==70000);
  for(unsigned t=0;t<120000;t+=500){
    assert(skit::digits(t)==skit::digits(t+skit::kCycleMs));
    assert(skit::price(t)==skit::price(t+skit::kCycleMs));
  }
  for(unsigned t=48000;t<70000;t+=500)assert(skit::digits(t)==1);
  assert(skit::digits(70000)==81000&&skit::digits(118000)==1);
  assert(config::kWindowSeconds[skit::kWindow]==30);
  btc::CandleSeries series;btc::CandleScale scale;
  skit::seedChart(series,scale);
  auto initial=btc::makeCandles(series,scale,skit::kWindow,skit::kChartStart);
  assert(initial.valid==0x1fffff);
  unsigned low=12,high=0;bool varied=false;
  auto chart=initial.plot();
  for(unsigned c=0;c<21;++c){
    unsigned height=0;
    for(unsigned r=0;r<13;++r)if(btc::plotPixel(chart,r,c)){++height;low=std::min(low,r);high=std::max(high,r);}
    assert(height<=2); // No giant ordinary opening candles.
    if(c&&chart.connectors[c]!=chart.connectors[c-1])varied=true;
  }
  assert(varied&&high-low>=2&&high-low<=4);
  double initialStep=scale.step;
  for(unsigned t=0;t<=35000;t+=500){
    auto stamp=skit::kChartStart+t;
    series.add(stamp,skit::price(t),skit::kWindow,stamp);
    scale.update(series,skit::kWindow,stamp);
    auto frame=btc::makeCandles(series,scale,skit::kWindow,stamp);
    assert(frame.valid==0x1fffff);
  }
  assert(scale.step>initialStep*100); // Spike compresses initial volatility.
}
