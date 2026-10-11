#include <cassert>
#include <iostream>
#include <set>
#include <time.h>
#include "Arduino.h"
#include "WiFi.h"
#include "SPI.h"
#include "MarketClient.h"
#include "Provisioning.h"
#include "OtaUpdate.h"
#include "SPIFFS.h"
#include "LedMatrixMap.h"
#include "LivePrice.h"

SerialMock Serial;EspMock ESP;WifiMock WiFi;uint32_t testMillis=100;
std::map<int,int>pins,modes,adc;std::vector<GpioEvent>gpioEvents;std::vector<SpiPacket>packets;
time_t testEpoch=1791408754;
time_t testTime(time_t* output){if(output)*output=testEpoch;return testEpoch;}
namespace market {
Request outstanding;Result queued;bool sent=false,ready=false;unsigned calls=0;
bool begin(){return true;}bool send(const Request&r){assert(!sent);outstanding=r;sent=true;++calls;return true;}
bool receive(Result&r){if(!ready)return false;r=queued;ready=sent=false;return true;}
void complete(bool ok=true,int status=200){queued={};queued.request=outstanding;queued.ok=ok;queued.status=status;queued.receivedAt=testMillis;queued.epoch=uint32_t(testEpoch);queued.quote.price=83300.0;queued.quote.usdtUsd=1;queued.quote.timestamp=uint32_t(testEpoch);
  if(outstanding.kind==Kind::History){uint32_t step=config::kCandleSeconds[outstanding.window];for(uint32_t close=outstanding.start+step;close<=outstanding.epoch;close+=step)assert(queued.history.add(close,83300));}
  ready=true;}
}
namespace ota {void begin(){}void healthCheck(uint32_t,bool){}bool checkDue(uint32_t,bool){return false;}void defer(uint32_t){}bool checkAndInstall(){return true;}}
namespace live {
Tick pending;bool queued=false,healthy=false,busy=false;
bool begin(){return true;}bool receive(Tick& tick){if(!queued)return false;tick=pending;queued=false;return true;}
Status status(){return Status{};}bool fresh(uint32_t){return healthy;}
bool pauseAndWait(uint32_t){return true;}void resume(){}void httpBusy(bool value){busy=value;}
}
#define time testTime
#include "../../src/main.cpp"
#undef time

void settingsTests(){
  pins[0]=HIGH;adc[34]=adc[35]=926;setup();
  packets.clear();draw(testMillis);unsigned matrices=0,zeros=0;
  for(const auto&p:packets){if(p.bus==HSPI)++matrices;else {assert(p.selected==16&&p.bytes[10]==0xC0);++zeros;}}
  assert(matrices==8&&zeros==1&&nextDrawAt==testMillis+25);
  testMillis+=100;packets.clear();draw(testMillis);assert(packets.size()==2&&packets[0].selected==16&&packets[1].selected==17);
  // A request finishing after Wi-Fi loss must not replace the setup animation.
  guard.price=83300;guard.acceptedAt=testMillis;
  market::outstanding={market::Kind::Quote,3,1,uint32_t(testEpoch)};market::sent=true;inflight=true;market::complete();
  packets.clear();processResult(testMillis);assert(packets.empty());guard=btc::PriceGuard{};
  WebServer&web=*servers().back();web.request("/",HTTP_GET);std::string marker="name=\"token\" value=\"";size_t pos=web.body.value.find(marker);assert(pos!=std::string::npos);
  web.request("/connect",HTTP_POST,{{"ssid","home"},{"password","password123"},{"token",web.body.value.substr(pos+marker.size(),32)}});
  assert(wifi.setupState()==btc::WifiSetupState::Connecting);testMillis+=25;packets.clear();draw(testMillis);
  unsigned nixies=0;for(const auto&p:packets)if(p.bus==VSPI)++nixies;assert(nixies==0&&packets.size()==8);
  WiFi.connection=WL_CONNECTED;
  wifi.loop(testMillis);schedule(testMillis);assert(market::sent&&market::outstanding.kind==market::Kind::Fx&&market::outstanding.source==1);
  assert(wifi.setupState()==btc::WifiSetupState::Connected);testMillis+=25;packets.clear();draw(testMillis);
  for(const auto&p:packets)if(p.bus==VSPI)for(int digit=1;digit<16;++digit)assert(p.bytes[digit]==0x80);
  market::complete();processResult(testMillis);assert(!guard.price);
  testMillis+=501;schedule(testMillis);assert(market::outstanding.kind==market::Kind::Quote&&market::outstanding.source!=1);
  market::complete();processResult(testMillis);assert(guard.price==83300);
  packets.clear();refreshNixies(testMillis);draw(testMillis);unsigned liveDigits=0;for(const auto&p:packets)if(p.bus==VSPI)++liveDigits;assert(liveDigits==6);
  testMillis+=8001;wifi.loop(testMillis);assert(!wifi.portalActive());packets.clear();draw(testMillis);assert(!showingSetup&&nextDrawAt==testMillis+1000);
  assert(display::startupPending()); // A live quote alone must not start the chart reveal.
  schedule(testMillis);assert(market::outstanding.kind==market::Kind::History);
  unsigned before=market::calls;
  adc[35]=1800;adc[34]=503;readDials(testMillis);testMillis+=151;readDials(testMillis);
  assert(refreshDial.stable==4&&timelineDial.stable==1);schedule(testMillis);assert(market::calls==before);
  Serial.input="dials\n";serialCommands(testMillis);assert(Serial.log.find("Frequency JSEL2 GPIO35=1800 mV; range JSEL1 GPIO34=503 mV")!=std::string::npos);
  market::complete();processResult(testMillis);
  packets.clear();draw(testMillis);assert(display::startupAnimating()&&nextDrawAt==testMillis+config::kStartupFrameMs);
  testMillis+=273*config::kStartupCellMs;draw(testMillis);assert(display::startupAnimating());
  testMillis+=273*config::kStartupCellMs;draw(testMillis);assert(!display::startupAnimating()&&nextDrawAt==testMillis+1000);
  for(int setting=0;setting<5;++setting){
    testMillis+=6000;refreshDial.stable=setting;fx.usd=1;fx.receivedAt=testMillis;nextQuoteAt=testMillis;
    for(uint32_t&deadline:nextHistoryAt)deadline=testMillis+1000000;
    schedule(testMillis);assert(market::outstanding.kind==market::Kind::Quote);assert(nextQuoteAt==testMillis+config::kRefreshMs[setting]);
    market::complete();processResult(testMillis);unsigned calls=market::calls;testMillis+=100;schedule(testMillis);assert(market::calls==calls);
  }
  refreshDial.stable=4;testMillis+=61000;nextQuoteAt=testMillis+200000;fx.receivedAt=testMillis-61000;
  schedule(testMillis);assert(market::outstanding.kind==market::Kind::Fx);double oldPrice=guard.price;uint32_t oldReceived=lastQuoteReceived;
  market::complete();market::queued.quote.price=90000;processResult(testMillis);assert(guard.price==oldPrice&&lastQuoteReceived==oldReceived);
  packets.clear();lastQuoteReceived=testMillis-btc::staleAfterMs(config::kRefreshMs[4])-1;draw(testMillis);assert(!packets.empty());
  // A large quote waits for another provider rather than lighting an outlier.
  market::outstanding={market::Kind::Quote,3,1,uint32_t(testEpoch)};market::sent=true;inflight=true;market::complete();market::queued.quote.price=150000;processResult(testMillis);assert(guard.price==oldPrice);
}
void cacheRuntimeTests(){
  testEpoch=1800000000;testMillis+=6000;timelineDial.stable=4;refreshDial.stable=4;fx.usd=1;fx.receivedAt=testMillis;
  guard.price=83300;guard.acceptedAt=testMillis;lastQuoteReceived=testMillis;lastPriceEpoch=uint32_t(testEpoch);
  for(auto&state:rotation.states){state.availableAt=testMillis;state.failures=0;}
  histories[4].count=0;for(uint32_t close=btc::firstHistoryClose(4,uint32_t(testEpoch));close<=testEpoch;close+=3600)assert(histories[4].add(close,83300));
  historyDirty[4]=true;saveHistory(testMillis);assert(!historyDirty[4]&&historyStore.savedEpoch(4)==testEpoch);
  unsigned writes=fsMock().writes;saveHistory(testMillis);assert(fsMock().writes==writes);
  nextQuoteAt=testMillis+1800000;nextHistoryAt[4]=testMillis;unsigned calls=market::calls;schedule(testMillis);assert(market::calls==calls);
  // Switching away and back changes draw timing, without refetching a complete cache.
  adc[34]=1336;readDials(testMillis);testMillis+=151;readDials(testMillis);assert(timelineDial.stable==3);
  adc[34]=1800;testMillis+=25;readDials(testMillis);testMillis+=151;readDials(testMillis);assert(timelineDial.stable==4);
  nextQuoteAt=testMillis+1800000;fx.receivedAt=testMillis;schedule(testMillis);assert(market::calls==calls);
  // The actual main scheduler requests only the missing two days.
  testEpoch+=2*86400;testMillis+=6000;nextHistoryAt[4]=testMillis;schedule(testMillis);
  assert(market::sent&&market::outstanding.kind==market::Kind::History&&market::outstanding.start==1800000000);
  assert(histories[4].count==121);market::complete();processResult(testMillis);assert(histories[4].count==169);saveHistory(testMillis);
  assert(historyStore.savedEpoch(4)==testEpoch&&!historyDirty[4]);
  // Before SNTP, a restored chart renders while current Nixies remain blank.
  testEpoch=0;guard=btc::PriceGuard{};lastPriceEpoch=0;nextDrawAt=testMillis;
  display::begin(); // A new boot must reveal a saved chart without a network fetch.
  display::price(83300);display::message("SYNC","CLOCK",testMillis);packets.clear();draw(testMillis);
  assert(!packets.empty());for(const auto&p:packets)if(p.bus==VSPI)for(int digit=1;digit<16;++digit)assert(p.bytes[digit]==0x80);
  assert(display::startupAnimating());
  testMillis+=273*config::kStartupCellMs;draw(testMillis);assert(display::startupAnimating());
  testMillis+=273*config::kStartupCellMs;packets.clear();draw(testMillis);assert(!display::startupAnimating());
  uint8_t registers[6][8]={};unsigned frames=0;
  for(const auto&p:packets)if(p.bus==HSPI){++frames;for(unsigned slot=0;slot<6;++slot)registers[config::kMaxShiftOrder[slot]][p.bytes[slot*2]-1]=p.bytes[slot*2+1];}
  assert(frames==8);auto chart=btc::makePlot(histories[4],historyStore.savedEpoch(4),config::kWindowSeconds[4],0,0);
  for(uint8_t row=0;row<13;++row)for(uint8_t col=0;col<21;++col){auto p=led_matrix::mapPixel(row,col);assert(bool(registers[p.driver][p.digit]&(1U<<p.segmentBit))==bool((chart.valid&(1U<<col))&&chart.rows[col]==row));}
  guard.price=83300;guard.acceptedAt=testMillis;lastPriceEpoch=1800172800;testEpoch=1800172800;
  std::cout<<"PASS: actual runtime cache persistence, instant range recall, no redundant writes/fetches, two-day suffix repair and saved chart before SNTP\n";
}
void soak(){
  // Two hours across the millis() rollover, including dial changes, transient
  // failures, source rotation, periodic history, FX and unchanged frames.
  uint32_t start=UINT32_MAX-100000;testMillis=start;
  nextQuoteAt=nextDialsAt=nextDrawAt=start;fx.receivedAt=start;fx.usd=1;guard.acceptedAt=start;lastQuoteReceived=start;
  for(auto&state:rotation.states){state.availableAt=start;state.failures=0;}
  for(auto&deadline:nextHistoryAt)deadline=start;
  std::set<unsigned> sources;unsigned failures=0;
  for(uint32_t elapsed=0;elapsed<7200000;elapsed+=100){
    testMillis=start+elapsed;testEpoch=1791408754+elapsed/1000;
    int position=(elapsed/60000)%5;adc[35]=config::kRefreshMv[position];adc[34]=config::kTimelineMv[(position+1)%5];
    if(market::sent){sources.insert(market::outstanding.source);bool fail=market::calls%17==0;if(fail)++failures;market::complete(!fail,fail?429:200);}
    loop();
    if(elapsed%60000==0){packets.clear();gpioEvents.clear();Serial.log.clear();}
  }
  assert(sources.size()==11&&failures>0&&guard.price>0&&refreshDial.initialized&&timelineDial.initialized);
  std::cout<<"PASS: five refresh settings, FX/display separation, responsive dials during requests, 2-hour runtime across millis rollover ("<<market::calls<<" requests; "<<failures<<" injected rate limits)\n";
}
void assertPriceFrame(double value){
  uint8_t digits[6];assert(btc::priceDigits(value,digits));assert(packets.size()==6);
  for(unsigned module=0;module<6;++module){auto&p=packets[module];assert(p.bus==VSPI&&p.selected==config::kNixieCs[module]&&p.bytes.size()==16&&p.bytes[0]==0xAA);
    for(unsigned digit=1;digit<16;++digit)assert(p.bytes[digit]==((digit==unsigned(digits[module]?digits[module]:10))?0xC0:0x80));}
}
void nixieCadenceTests(){
  assert(wifi.online()&&!wifi.portalActive());
  uint32_t initial=testMillis;
  for(unsigned setting=0;setting<5;++setting){
    uint32_t period=config::kRefreshMs[setting],start=initial+setting*10000000;
    testMillis=start;refreshDial.stable=setting;guard=btc::PriceGuard{};guard.accept(83300,0,start);assert(guard.accept(83300,1,start));
    shownPrice=0;lastQuoteReceived=start;nextNixieAt=start;needConfirmation=false;
    packets.clear();refreshNixies(start);assertPriceFrame(83300);uint32_t frames=nixieFrames;
    // A history/TLS request can remain blocked while the independent stream
    // updates the latest value. It cannot block or accelerate tube frames.
    inflight=true;market::sent=true;
    testMillis=start+period/2;
    for(uint8_t source:{uint8_t(1),uint8_t(0)}){live::pending.quote.price=83342;live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.receivedAt=testMillis;live::pending.source=source;live::queued=true;processLive(testMillis);}
    assert(guard.price==83342&&shownPrice==83300);
    packets.clear();refreshNixies(testMillis);assert(packets.empty()&&nixieFrames==frames);
    testMillis=start+period-1;refreshNixies(testMillis);assert(packets.empty());
    testMillis=start+period;refreshNixies(testMillis);assertPriceFrame(83342);assert(nixieFrames==frames+1);
    packets.clear();testMillis=start+2*period;refreshNixies(testMillis);assertPriceFrame(83342);assert(nixieFrames==frames+2); // Identical digits still refresh all six tubes.
    inflight=false;market::sent=false;
  }
  // A dial change takes effect immediately even with a network request active.
  testMillis+=100;refreshDial.stable=4;refreshDial.candidate=-1;nextDialsAt=testMillis;
  inflight=true;adc[35]=0;readDials(testMillis);testMillis+=151;readDials(testMillis);assert(refreshDial.stable==0&&nextNixieAt==testMillis);
  lastQuoteReceived=testMillis;packets.clear();refreshNixies(testMillis);assertPriceFrame(83342);assert(nextNixieAt==testMillis+500);inflight=false;
  // Once a stream is fresh, REST traffic is only needed for confirmation or fallback.
  needConfirmation=false;live::healthy=true;fx.usd=1;fx.receivedAt=testMillis;nextQuoteAt=testMillis;for(auto&deadline:nextHistoryAt)deadline=testMillis+100000;
  unsigned calls=market::calls;schedule(testMillis);assert(market::calls==calls);
  live::healthy=false;for(auto&state:rotation.states)state.availableAt=testMillis;schedule(testMillis);assert(market::calls==calls+1&&live::busy);
  market::complete();processResult(testMillis);assert(!live::busy);
  // Cadence and stale blanking remain correct across millis rollover.
  testMillis=UINT32_MAX-249;lastQuoteReceived=testMillis;shownPrice=0;packets.clear();refreshNixies(testMillis);assertPriceFrame(guard.price);
  packets.clear();testMillis=249;refreshNixies(testMillis);assert(packets.empty());
  testMillis=250;refreshNixies(testMillis);assertPriceFrame(guard.price);
  packets.clear();testMillis+=btc::staleAfterMs(500)+1;refreshNixies(testMillis);assert(shownPrice==0&&packets.size()==6);
  for(auto&p:packets)for(unsigned digit=1;digit<16;++digit)assert(p.bytes[digit]==0x80);
  lastQuoteReceived=testMillis;packets.clear();refreshNixies(testMillis);assertPriceFrame(guard.price);
  // A late loop emits one current frame, without fake catch-up frames or timer drift.
  uint32_t deadline=nextNixieAt;testMillis=deadline+1005;lastQuoteReceived=testMillis;packets.clear();refreshNixies(testMillis);assertPriceFrame(guard.price);assert(nextNixieAt==deadline+1500);
  std::cout<<"PASS: actual Nixie SPI frames at all five selected intervals, unchanged digits, newest stream values, blocked history/TLS, immediate dial changes, REST fallback, outage blanking and millis rollover\n";
}
int main(){settingsTests();cacheRuntimeTests();nixieCadenceTests();soak();}
