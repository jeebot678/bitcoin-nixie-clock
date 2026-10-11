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
namespace ota {bool due=false;void begin(){}void healthCheck(uint32_t,bool){}bool checkDue(uint32_t,bool){return due;}void defer(uint32_t){due=false;}bool checkAndInstall(){return true;}}
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
  wifi.loop(testMillis);schedule(testMillis);assert(!market::sent&&streamWarmupStarted);
  testMillis+=4999;schedule(testMillis);assert(!market::sent);
  testMillis+=1;schedule(testMillis);assert(market::sent&&market::outstanding.kind==market::Kind::Quote&&!btc::kSources[market::outstanding.source].usdt);
  assert(wifi.setupState()==btc::WifiSetupState::Connected);testMillis+=25;packets.clear();draw(testMillis);
  assert(display::startupPending());
  market::complete();processResult(testMillis);assert(guard.price==83300&&!guard.confirmed()&&needConfirmation);
  assert(nextQuoteAt==testMillis);
  packets.clear();refreshNixies(testMillis);draw(testMillis);unsigned liveDigits=0;for(const auto&p:packets)if(p.bus==VSPI)++liveDigits;assert(liveDigits==6);
  testMillis+=8001;wifi.loop(testMillis);assert(!wifi.portalActive());packets.clear();draw(testMillis);assert(!showingSetup&&nextDrawAt==testMillis+1000);
  assert(display::startupPending()); // A live quote alone must not start the chart reveal.
  schedule(testMillis);assert(!market::sent); // Background confirmation must not cut off stream startup.
  testMillis+=25000;schedule(testMillis);assert(market::sent); // Stream grace is bounded.
  // An independent live quote confirms the initial source in the background.
  live::pending.quote.price=83300;live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.receivedAt=testMillis;live::pending.source=1;live::queued=true;processLive(testMillis);
  assert(guard.confirmed()&&!needConfirmation);market::complete();processResult(testMillis);
  schedule(testMillis);assert(market::outstanding.kind==market::Kind::History);
  unsigned before=market::calls;
  adc[35]=1800;adc[34]=503;readDials(testMillis);testMillis+=151;readDials(testMillis);
  assert(refreshDial.stable==4&&timelineDial.stable==1);schedule(testMillis);assert(market::calls==before);
  Serial.input="dials\n";serialCommands(testMillis);assert(Serial.log.find("Frequency JSEL2 GPIO35=1800 mV; range JSEL1 GPIO34=503 mV")!=std::string::npos);
  Serial.input="ticks on\nticks on\n";serialCommands(testMillis);assert(logNixies);
  Serial.input="ticks off\nticks off\n";serialCommands(testMillis);assert(!logNixies);
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
    quotes.clear();shownPrice=0;lastQuoteReceived=start;nextNixieAt=start;needConfirmation=false;
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
    packets.clear();testMillis=start+2*period;refreshNixies(testMillis);assertPriceFrame(setting==0?83343:83342);assert(nixieFrames==frames+2); // Only fast mode adds bounded cosmetic motion.
    inflight=false;market::sent=false;
  }
  // A dial change takes effect immediately even with a network request active.
  testMillis+=100;refreshDial.stable=4;refreshDial.candidate=-1;nextDialsAt=testMillis;
  inflight=true;adc[35]=0;readDials(testMillis);testMillis+=151;readDials(testMillis);assert(refreshDial.stable==0&&nextNixieAt==testMillis);
  lastQuoteReceived=testMillis;packets.clear();refreshNixies(testMillis);assertPriceFrame(83343);assert(nextNixieAt==testMillis+500);inflight=false;
  // Once a stream is fresh, REST traffic is only needed for confirmation or fallback.
  needConfirmation=false;live::healthy=true;fx.usd=1;fx.receivedAt=testMillis;nextQuoteAt=testMillis;for(auto&deadline:nextHistoryAt)deadline=testMillis+100000;
  unsigned calls=market::calls;schedule(testMillis);assert(market::calls==calls);
  live::healthy=false;for(auto&state:rotation.states)state.availableAt=testMillis;schedule(testMillis);assert(market::calls==calls+1&&live::busy);
  market::complete();processResult(testMillis);assert(!live::busy);
  // Cadence and stale blanking remain correct across millis rollover.
  testMillis=UINT32_MAX-249;lastQuoteReceived=testMillis;shownPrice=0;packets.clear();refreshNixies(testMillis);assertPriceFrame(guard.price);
  packets.clear();testMillis=249;refreshNixies(testMillis);assert(packets.empty());
  testMillis=250;refreshNixies(testMillis);assertPriceFrame(guard.price+1);
  packets.clear();testMillis+=btc::staleAfterMs(500)+1;refreshNixies(testMillis);assert(shownPrice==0&&packets.size()==6);
  for(auto&p:packets)for(unsigned digit=1;digit<16;++digit)assert(p.bytes[digit]==0x80);
  lastQuoteReceived=testMillis;packets.clear();refreshNixies(testMillis);assertPriceFrame(guard.price);
  // A late loop emits one current frame, without fake catch-up frames or timer drift.
  uint32_t deadline=nextNixieAt;testMillis=deadline+1005;lastQuoteReceived=testMillis;packets.clear();refreshNixies(testMillis);assertPriceFrame(guard.price+1);assert(nextNixieAt==deadline+1500);
  // Confirming the default detent must not emit an early same-frequency frame.
  deadline=nextNixieAt;refreshDial.initialized=false;refreshDial.candidate=-1;
  nextDialsAt=testMillis;adc[35]=0;readDials(testMillis);testMillis+=151;readDials(testMillis);
  assert(refreshDial.initialized&&refreshDial.stable==0&&nextNixieAt==deadline);
  std::cout<<"PASS: actual Nixie SPI frames at all five selected intervals, unchanged digits, newest stream values, blocked history/TLS, immediate dial changes, REST fallback, outage blanking and millis rollover\n";
}
void quoteSelectionRuntimeTests(){
  testMillis+=6000;refreshDial.stable=0;guard=btc::PriceGuard{};quotes.clear();shownPrice=0;
  auto tick=[&](uint8_t provider,double value){live::pending.quote.price=value;live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.receivedAt=testMillis;live::pending.source=provider;live::queued=true;processLive(testMillis);};
  tick(0,80010.25);tick(1,80017.6);tick(0,80010.25);
  for(unsigned i=0;i<100;++i)tick(1,80017.6);
  packets.clear();refreshNixies(testMillis);assertPriceFrame(80018);
  testMillis+=500;tick(1,80017.6);packets.clear();refreshNixies(testMillis);assertPriceFrame(80010);
  assert(shownSource==0&&shownPrice==80010.25&&guard.price==80017.6&&lastPriceEpoch==uint32_t(testEpoch));
  testMillis+=500;tick(1,80017.6);packets.clear();refreshNixies(testMillis);assertPriceFrame(80018);
  // Expired alternative quotes cannot be used to create apparent movement.
  testMillis+=5000;tick(1,80017.6);packets.clear();refreshNixies(testMillis);assertPriceFrame(80017);
  assert(guard.price==80017.6&&shownPrice==80017.6&&lastPriceEpoch==uint32_t(testEpoch));
  quotes.clear();
  std::cout<<"PASS: actual runtime/SPI source rotation survives burst overwrite; expired alternatives yield bounded cosmetic rendering without changing chart price/timestamp\n";
}
void startupAndDialRegressionTests(){
  testMillis+=6000;refreshDial=btc::Dial(0);guard=btc::PriceGuard{};quotes.clear();shownPrice=0;
  display::blankPrice();packets.clear();refreshNixies(testMillis);
  assert(packets.size()==1&&packets[0].selected==16&&packets[0].bytes[10]==0xC0);
  testMillis+=100;packets.clear();refreshNixies(testMillis);draw(testMillis);
  assert(packets.size()==2&&packets[0].selected==16&&packets[1].selected==17);
  // The first valid quote must replace the startup chase on this same loop,
  // even if the dial previously had a deadline an hour away and no other
  // exchange has connected yet.
  nextNixieAt=testMillis+3600000;
  auto tick=[&](double price){live::pending.quote.price=price;live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.receivedAt=testMillis;live::pending.source=0;live::queued=true;processLive(testMillis);};
  tick(80000.25);assert(guard.price==80000.25&&!guard.confirmed()&&needConfirmation);
  packets.clear();refreshNixies(testMillis);assertPriceFrame(80000);assert(nextNixieAt==testMillis+500);
  testMillis+=500;tick(80000.25);packets.clear();refreshNixies(testMillis);assertPriceFrame(80001);
  testMillis+=500;tick(80000.25);packets.clear();refreshNixies(testMillis);assertPriceFrame(80000);
  uint32_t received=lastQuoteReceived;uint32_t last=shownDigits;
  for(unsigned frame=0;frame<24;++frame){ // 12-second reconnect, no fabricated input ticks.
    testMillis+=500;packets.clear();refreshNixies(testMillis);assert(shownDigits!=last);
    assertPriceFrame(shownDigits);assert(std::abs(double(shownDigits)-80000.25)<=1);
    assert(lastQuoteReceived==received&&shownQuoteReceived==received&&guard.price==80000.25);last=shownDigits;
  }
  // Replay observed readings at the fast contact after a stable 1-hour
  // setting. All readings represent the same detent, so noise must not
  // keep resetting its debounce or strand the display at the old rate.
  refreshDial=btc::Dial(4);refreshDial.initialized=true;nextNixieAt=testMillis+3600000;nextDialsAt=testMillis;
  for(uint32_t measured:{142U,301U,309U,222U,237U,182U,309U}){
    adc[35]=measured;readDials(testMillis);testMillis+=25;
  }
  assert(refreshDial.initialized&&refreshDial.stable==0&&nextNixieAt==testMillis-25);
  tick(80000.25);packets.clear();refreshNixies(testMillis);assertPriceFrame(80001);
  uint32_t deadline=nextNixieAt;assert(deadline==testMillis+475);
  testMillis=deadline-1;packets.clear();refreshNixies(testMillis);assert(packets.empty());
  testMillis=deadline;tick(80000.25);refreshNixies(testMillis);assertPriceFrame(80000);
  // Background confirmation must leave a previously lit value usable.
  btc::Quote independent;independent.price=80005;independent.timestamp=uint32_t(testEpoch);
  acceptQuote(independent,1,testMillis,uint32_t(testEpoch),testMillis);
  assert(guard.confirmed()&&!needConfirmation&&guard.price==80005);
  // First-price/confirmation network deadlines cannot inherit a 1-hour dial.
  guard=btc::PriceGuard{};quotes.clear();refreshDial.stable=4;nextQuoteAt=testMillis+3600000;
  tick(80000.25);assert(!guard.confirmed()&&nextQuoteAt==testMillis);
  liveReady=false;streamWarmupStarted=true;streamWarmupUntil=testMillis-1;
  for(auto& state:rotation.states){state.availableAt=testMillis;state.failures=0;}
  nextNixieAt=testMillis+3600000;shownPrice=0;packets.clear();refreshNixies(testMillis);assertPriceFrame(80000);
  schedule(testMillis);assert(market::sent&&market::outstanding.kind==market::Kind::Quote&&!btc::kSources[market::outstanding.source].usdt);
  market::complete();processResult(testMillis);
  // OTA still runs if market data never arrives, so a network/source outage
  // cannot strand the clock on old firmware.
  guard=btc::PriceGuard{};ota::due=true;schedule(testMillis);
  assert(market::sent&&market::outstanding.kind==market::Kind::Ota&&!ota::due);
  market::complete();processResult(testMillis);liveReady=true;
  std::cout<<"PASS: first-source display, startup chase, 500 ms tube packets during a 12-second reconnect without new ticks, observed dial noise recovering from 1-hour frequency, dial-independent confirmation and OTA without market data\n";
}
void tubeDiagnosticTests(){
  guard=btc::PriceGuard{};quotes.clear();testMillis+=6000;Serial.input="tube-test\n";serialCommands(testMillis);
  uint32_t start=testMillis;display::blankPrice();
  for(unsigned step=0;step<10;++step){
    testMillis=start+step*500;packets.clear();refreshNixies(testMillis);
    assert(packets.size()==6);
    for(unsigned module=0;module<6;++module){auto&p=packets[module];unsigned digit=(step+module)%10;
      assert(p.selected==config::kNixieCs[module]&&p.bytes[0]==0xAA&&p.bytes.size()==16);
      for(unsigned field=1;field<16;++field)assert(p.bytes[field]==((field==(digit?digit:10))?0xC0:0x80));}
    packets.clear();testMillis+=499;refreshNixies(testMillis);assert(packets.empty());
    draw(testMillis);for(const auto&p:packets)assert(p.bus==HSPI); // Setup/stale messages must not overwrite the diagnostic.
  }
  testMillis=start+5000;packets.clear();refreshNixies(testMillis);assert(!tubeTestEndsAt);
  assert(packets.size()==6&&packets.back().selected==16&&packets.back().bytes[10]==0xC0); // Return to startup chase with no market.
  btc::Quote tick;tick.price=83300;tick.timestamp=uint32_t(testEpoch);acceptQuote(tick,0,testMillis,uint32_t(testEpoch),testMillis);
  packets.clear();refreshNixies(testMillis);assertPriceFrame(83300);assert(!tubeTestEndsAt);
  std::cout<<"PASS: explicit five-second tube diagnostic advances every cathode on all six CS lines at 500 ms without prices, then restores the live/startup display\n";
}
int main(){settingsTests();cacheRuntimeTests();nixieCadenceTests();quoteSelectionRuntimeTests();startupAndDialRegressionTests();tubeDiagnosticTests();soak();}
