#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include "CandleStore.h"
#include "MarketParsers.h"
#include "SPIFFS.h"
#include "SPI.h"
#include "Display.h"
#include "LedMatrixMap.h"
SerialMock Serial;EspMock ESP;uint32_t testMillis=100;
std::map<int,int>pins,modes,adc;std::vector<GpioEvent>gpioEvents;std::vector<SpiPacket>packets;
struct TradeReader{std::istringstream input;explicit TradeReader(const std::string& text):input(text){}int read(){return input.get();}};
const uint64_t utc=1800000000123ULL;
btc::CandleSeries populated(unsigned w,uint64_t now=utc){
  btc::CandleSeries s;uint32_t current=btc::candleBucket(now,w);
  for(unsigned col=0;col<21;++col){uint64_t t=btc::candleStart(current-20+col,w);double p=80000+col;
    assert(s.add(t,p,w,now));assert(s.add(t+1,p+10,w,now));assert(s.add(t+2,p-5,w,now));assert(s.add(t+3,p+3,w,now));}
  return s;
}
void aggregation(){
  for(unsigned w=0;w<5;++w){
    auto s=populated(w);uint32_t id=btc::candleBucket(utc,w);assert(s.count(w,utc)==21);
    for(const auto& c:s.bars){assert(btc::validCandle(c,w));assert(c.high==c.open+1000&&c.low==c.open-500&&c.close==c.open+300);}
    auto& closed=s.bars[(id-1)%21];auto previous=closed;
    assert(s.add(utc,80044,w,utc));assert(!memcmp(&previous,&closed,sizeof(closed)));
    // Repeated ticks do not move any boundary or closed candle's OHLC.
    assert(!s.add(utc,80044,w,utc));assert(!s.add(utc+1,80000,w,utc));
    assert(!s.add(btc::candleStart(id-21,w),80000,w,utc));
    btc::CandleScale scale;assert(scale.update(s,w,utc));auto old=btc::makeCandles(s,scale,w,utc);
    uint64_t next=btc::candleStart(id+1,w);assert(s.add(next,80045,w,next));
    auto shifted=btc::makeCandles(s,scale,w,next);assert(shifted.valid==0x1FFFFF);
    for(unsigned col=0;col<20;++col)assert(old.bodies[col+1]==shifted.bodies[col]&&old.wicks[col+1]==shifted.wicks[col]);
    s.trim(w,next);assert(s.count(w,next)==21);
    // Out-of-order trades correctly recover the earliest open and latest close.
    btc::CandleSeries ordered;uint64_t start=btc::candleStart(id,w);
    assert(ordered.add(start+10,80000,w,utc));assert(ordered.add(start+5,80002,w,utc));assert(ordered.add(start+11,80004,w,utc));
    auto c=ordered.bars[id%21];assert(c.open==8000200&&c.close==8000400&&c.low==8000000&&c.high==8000400);
    auto copy=ordered;assert(!copy.add(start+10,0,w,utc)&&!copy.add(start+10,NAN,w,utc));assert(!memcmp(&copy,&ordered,sizeof(copy)));
  }
}
void scaleTests(){
  auto s=populated(4);btc::CandleScale scale;scale.update(s,4,utc);double step=scale.step,low=scale.low;
  auto before=btc::makeCandles(s,scale,4,utc);uint32_t current=btc::candleBucket(utc,4);
  for(unsigned tick=1;tick<=40;++tick){assert(s.add(utc+tick*100,80035+tick*.01,4,utc+tick*100));scale.update(s,4,utc+tick*100);assert(scale.step==step&&scale.low==low);}
  auto after=btc::makeCandles(s,scale,4,utc+4000);
  for(unsigned col=0;col<20;++col)assert(before.bodies[col]==after.bodies[col]&&before.wicks[col]==after.wicks[col]);
  // A modest trend pans one whole row per 5 seconds without resizing height.
  btc::CandleSeries trend;assert(trend.add(utc,low+step*9,4,utc));btc::CandleScale pan=scale;pan.lastPan=uint32_t(utc/1000);pan.lastEpoch=pan.lastPan;
  pan.update(trend,4,utc+4000);assert(pan.low==low&&pan.step==step);
  pan.update(trend,4,utc+5000);assert(std::abs(pan.low-low-step)<1e-6&&pan.step==step);
  pan.update(trend,4,utc+9000);assert(std::abs(pan.low-low-step)<1e-6);
  // A genuine large spike expands on this frame and includes its extreme.
  assert(s.add(utc+5000,80500,4,utc+5000));scale.update(s,4,utc+5000);assert(scale.step>step&&scale.low+scale.step*12>=80500);
  // A 30-minute EMA responds to sustained range, not a single demand sample.
  btc::CandleSeries flat;flat.add(utc,80000,4,utc);btc::CandleScale slow;slow.low=79900;slow.step=20;slow.smoothedSpan=240;slow.lastEpoch=slow.lastPan=slow.lastResize=uint32_t(utc/1000);
  auto baseline=slow.step;slow.update(flat,4,utc+1000);assert(slow.step==baseline);
  for(unsigned second=2;second<=1800;++second){uint64_t now=utc+second*1000ULL;flat.add(now,80000,4,now);slow.update(flat,4,now);}
  double expected=2+(240-2)*std::exp(-1.0);assert(std::abs(slow.smoothedSpan-expected)<.01&&slow.step<baseline&&slow.step>baseline*.4);
  btc::CandleSeries volatileSeries;btc::CandleScale grow;grow.low=79940;grow.step=10;grow.smoothedSpan=120;grow.lastEpoch=grow.lastPan=grow.lastResize=uint32_t(utc/1000);
  for(unsigned second=1;second<=1800;++second){uint64_t now=utc+second*1000ULL;volatileSeries.add(now,79935,4,now);volatileSeries.add(now,80065,4,now);grow.update(volatileSeries,4,now);
    if(second<=600)assert(grow.step==10);}
  assert(grow.step>10&&grow.step<11.6); // Sustained volatility widens gradually without a spike.
  // Seconds without quotes and unsynchronized clocks cannot generate candles.
  auto empty=btc::makeCandles(btc::CandleSeries{},slow,4,utc);assert(!empty.valid);assert(current);
}
void persistence(){
  btc::CandleSeries all[5];btc::CandleScale scales[5];CandleStore store;assert(store.begin(all,scales));
  fsMock().files["/history-0-0.bin"]={1,2,3,4}; // Existing close cache must survive upgrade.
  for(unsigned w=0;w<5;++w){all[w]=populated(w);scales[w].update(all[w],w,utc);assert(store.save(w,all[w],scales[w],utc));}
  auto files=fsMock().files;btc::CandleSeries restored[5];btc::CandleScale axes[5];CandleStore boot;assert(boot.begin(restored,axes));
  for(unsigned w=0;w<5;++w){assert(!memcmp(&all[w],&restored[w],sizeof(all[w])));assert(scales[w].low==axes[w].low&&scales[w].step==axes[w].step&&scales[w].smoothedSpan==axes[w].smoothedSpan&&!axes[w].lastEpoch);}
  assert(fsMock().files["/history-0-0.bin"]==files["/history-0-0.bin"]);
  for(size_t cut=0;cut<652;++cut){fsMock().files=files;fsMock().writeBudget=cut;CandleStore write;assert(write.begin(restored,axes));assert(!write.save(0,all[0],scales[0],utc+1));fsMock().writeBudget=SIZE_MAX;CandleStore reboot;assert(reboot.begin(restored,axes));assert(reboot.savedEpoch(0)==utc/1000);assert(!memcmp(&all[0],&restored[0],sizeof(all[0])));}
  fsMock().files=files;
  // Every byte is CRC-covered; corrupt/short files cannot expose partial candles.
  for(size_t byte=0;byte<652;++byte){fsMock().files=files;fsMock().files["/candles-0-0.bin"][byte]^=1;CandleStore reader;reader.begin(restored,axes);assert(!reader.savedEpoch(0)&&!restored[0].count(0,utc));}
  fsMock().files=files;CandleStore reboot;reboot.begin(restored,axes);assert(restored[4].trim(4,utc+172800000)&&!restored[4].count(4,utc+172800000));
  assert(axes[4].valid()); // Price scale survives even when every old candle ages out.
}
void physicalPixels(){
  unsigned w=4;auto s=populated(w);btc::CandleScale scale;scale.update(s,w,utc);auto frame=btc::makeCandles(s,scale,w,utc);
  auto full=frame.plot();unsigned bodyPixels=0,fullPixels=0;
  for(unsigned col=0;col<21;++col)for(unsigned row=0;row<13;++row){bodyPixels+=bool(frame.bodies[col]&(1U<<row));fullPixels+=btc::plotPixel(full,row,col);}
  assert(bodyPixels>=21&&fullPixels>bodyPixels);
  display::begin();display::startupPlot(full,100);display::startupPlot(full,100+273*config::kStartupCellMs);packets.clear();display::startupPlot(full,100+546*config::kStartupCellMs);assert(!display::startupAnimating());
  uint8_t registers[6][8]={};for(const auto& p:packets)if(p.bus==HSPI)for(unsigned slot=0;slot<6;++slot)registers[config::kMaxShiftOrder[slot]][p.bytes[slot*2]-1]=p.bytes[slot*2+1];
  for(unsigned row=0;row<13;++row)for(unsigned col=0;col<21;++col){auto p=led_matrix::mapPixel(row,col);assert(bool(registers[p.driver][p.digit]&(1U<<p.segmentBit))==btc::plotPixel(full,row,col));}
  // Full candle pixels remain on through every former PWM phase.
  for(unsigned phase=0;phase<12;++phase){testMillis+=1;packets.clear();display::startupPlot(full,testMillis);assert(packets.empty());}
}
void minimumCells(){
  for(unsigned w=0;w<5;++w){
    btc::CandleSeries s;uint32_t current=btc::candleBucket(utc,w);
    for(unsigned col:{2U,8U,18U}){uint64_t t=btc::candleStart(current-20+col,w);assert(s.add(t,80000+col,w,utc));assert(s.add(t+1,80001+col,w,utc));}
    auto original=s;btc::CandleScale axis;axis.update(s,w,utc);double low=axis.low,step=axis.step;
    auto frame=btc::makeCandles(s,axis,w,utc);auto pixels=frame.plot();assert(frame.valid==0x1FFFFF);
    uint32_t carried=8000200;
    for(unsigned col=0;col<21;++col){const auto& c=s.bars[(current-20+col)%21];unsigned lit=0;for(unsigned row=0;row<13;++row)lit+=btc::plotPixel(pixels,row,col);assert(lit>=1);
      if(c.bucket==current-20+col)carried=c.close;
      else assert(lit==1&&frame.bodies[col]==uint16_t(1U<<axis.row(carried)));
    }
    assert(s.count(w,utc)==3&&!memcmp(&s,&original,sizeof(s))&&axis.low==low&&axis.step==step);
    uint32_t fallback=s.latestCloseCents(w);assert(fallback==8001900);assert(s.trim(w,utc+172800000));
    auto stale=btc::makeCandles(s,axis,w,utc+172800000,fallback);assert(stale.valid==0x1FFFFF&&!s.count(w,utc+172800000));
    for(unsigned col=0;col<21;++col)assert(__builtin_popcount(stale.plot().connectors[col])==1);
    btc::CandleSeries flat;assert(flat.add(utc,80000,w,utc));auto flatFrame=btc::makeCandles(flat,axis,w,utc);
    for(unsigned col=0;col<21;++col)assert(__builtin_popcount(flatFrame.plot().connectors[col])==1);
    assert(!btc::makeCandles(btc::CandleSeries{},axis,w,utc).valid);assert(!btc::makeCandles(flat,axis,w,0,8000000).valid);
  }
  std::cout<<"PASS: every candle column has a solid minimum cell; quiet/leading/trailing gaps and expired windows carry known prices without modifying OHLC or scale\n";
}
std::string fixture(const char* name){std::ifstream file(std::string("test/live_fixtures/")+name);std::ostringstream content;content<<file.rdbuf();assert(file.good());return content.str();}
void parserTests(){
  uint64_t now=1791846829000ULL;
  for(bool bitstamp:{false,true})for(unsigned w=0;w<5;++w){
    std::string text=fixture(bitstamp?"bitstamp_recent_trades.json":"coinbase_recent_trades.json");btc::CandleSeries c;btc::Quote latest;DynamicJsonDocument doc(512);TradeReader in(text);unsigned yields=0;
    assert(btc::parseTradeCandles(in,doc,bitstamp,w,now,c,latest,[&](){++yields;}));assert(c.count(w,now)&&latest.price>0);if(!bitstamp)assert(yields);
    text.pop_back();TradeReader truncated(text);assert(!btc::parseTradeCandles(truncated,doc,bitstamp,w,now,c,latest,[](){}));
  }
  std::ifstream reportFile("test/candle_trades_probe_report.json");DynamicJsonDocument reference(100000);assert(!deserializeJson(reference,reportFile));
  for(bool bitstamp:{false,true})for(unsigned w=0;w<5;++w){
    const char* name=bitstamp?"bitstamp":"coinbase";std::string filename=std::string(name)+"_candle_trades.json";
    std::string text=fixture(filename.c_str());TradeReader input(text);btc::CandleSeries candles;btc::Quote quote;DynamicJsonDocument pool(512);
    uint64_t utcMs=reference[name]["epoch"].as<uint64_t>()*1000;
    assert(btc::parseTradeCandles(input,pool,bitstamp,w,utcMs,candles,quote,[](){}));
    auto expected=reference[name]["expected_candles"][std::to_string(w)].as<JsonObjectConst>();assert(candles.count(w,utcMs)==expected.size());
    for(auto entry:expected){uint32_t id=uint32_t(strtoul(entry.key().c_str(),nullptr,10));const auto& bar=candles.bars[id%21];auto values=entry.value().as<JsonArrayConst>();
      assert(bar.bucket==id&&bar.open==values[0].as<uint32_t>()&&bar.high==values[1].as<uint32_t>()&&bar.low==values[2].as<uint32_t>()&&bar.close==values[3].as<uint32_t>());}
    if(bitstamp&&w==4)assert(candles.count(w,utcMs)==21);
  }
  assert(btc::isoTimestampMs("2026-10-12T23:13:49.123456Z")==now+123);assert(btc::isoTimestampMs("2026-10-12T23:13:49.1Z")==now+100);
  std::string text="[{\"date\":\"1791846828\",\"tid\":\"3\",\"price\":\"80003\"},{\"date\":\"1791846828\",\"tid\":\"1\",\"price\":\"80001\"},{\"date\":\"1791846828\",\"tid\":\"2\",\"price\":\"79999\"}]";
  TradeReader input(text);btc::CandleSeries c;btc::Quote q;DynamicJsonDocument doc(512);assert(btc::parseTradeCandles(input,doc,true,0,now,c,q,[](){}));
  auto bar=c.bars[btc::candleBucket(now-1000,0)%21];assert(bar.open==8000100&&bar.close==8000300&&bar.low==7999900&&bar.high==8000300&&q.price==80003);
  for(const char* bad:{"[{}]","[", "{}", "[] garbage", "[{\"date\":\"1791846828\",\"tid\":\"1\",\"price\":\"NaN\"}]"}){TradeReader malformed(bad);assert(!btc::parseTradeCandles(malformed,doc,true,0,now,c,q,[](){}));}
}
int main(){aggregation();scaleTests();persistence();physicalPixels();minimumCells();parserTests();std::cout<<"PASS: fixed UTC OHLC buckets, chronological trades, stable heights, bounded panning, spike expansion, 30-minute volatility response, durable axis/candles, corruption/power loss, real fixture streaming and physical candle SPI/startup pixels\n";}
