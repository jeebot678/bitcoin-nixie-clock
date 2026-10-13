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
#include "DeviceTelemetry.h"

SerialMock Serial;EspMock ESP;WifiMock WiFi;uint32_t testMillis=100;
std::map<int,int>pins,modes,adc;std::vector<GpioEvent>gpioEvents;std::vector<SpiPacket>packets;
time_t testEpoch=1791408754;
time_t testTime(time_t* output){if(output)*output=testEpoch;return testEpoch;}
namespace market {
Request outstanding;Result queued;bool sent=false,ready=false;unsigned calls=0;
CandleStore* savingStore=nullptr;btc::CandleSeries savingSeries;btc::CandleScale savingScale;uint64_t savingTime=0;
bool begin(){return true;}bool send(const Request&r){assert(!sent);outstanding=r;sent=true;++calls;return true;}
bool sendCache(CandleStore& store,uint8_t window,const btc::CandleSeries& series,const btc::CandleScale& scale,uint64_t epochMs){savingStore=&store;savingSeries=series;savingScale=scale;savingTime=epochMs;return send({Kind::SaveCache,0,window,uint32_t(epochMs/1000)});}
bool receive(ResultLease&r){if(!ready)return false;r.value_=&queued;ready=false;return true;}
ResultLease::~ResultLease(){if(value_)sent=false;}uint32_t stackRemaining(){return 4096;}
void complete(bool ok=true,int status=200){queued={};queued.request=outstanding;queued.ok=ok;queued.status=status;queued.receivedAt=testMillis;queued.epoch=uint32_t(testEpoch);queued.quote.price=83300.0;queued.quote.usdtUsd=1;queued.quote.timestamp=uint32_t(testEpoch);
  if(outstanding.kind==Kind::SaveCache){queued.ok=ok&&savingStore->save(outstanding.window,savingSeries,savingScale,savingTime);queued.cacheEpochMs=savingTime;}
  if(outstanding.kind==Kind::Candles){queued.candleAttemptMask=outstanding.source==2&&outstanding.window<2?0x03:0x1F;queued.candleCoverageMask=outstanding.source==2?queued.candleAttemptMask:0;queued.candleSnapshotMs=uint64_t(testEpoch)*1000;
    for(unsigned w=0;w<5;++w)if(queued.candleAttemptMask&(1U<<w)){uint32_t current=btc::candleBucket(uint64_t(testEpoch)*1000,w);for(unsigned col=0;col<21;++col)queued.candles[w].add(btc::candleStart(current-20+col,w),83300,w,uint64_t(testEpoch)*1000);}}

  if(outstanding.kind==Kind::History){uint32_t step=config::kCandleSeconds[outstanding.window];for(uint32_t close=outstanding.start+step;close<=outstanding.epoch;close+=step)assert(queued.history.add(close,83300));}
  ready=true;}
}
namespace ota {Status state;Status status(){return state;}bool due=false,validBoot=true;void begin(){}void healthCheck(uint32_t,bool){}bool bootConfirmed(){return validBoot;}bool checkDue(uint32_t,bool){return due;}void defer(uint32_t){due=false;}bool checkAndInstall(){return true;}}
namespace telemetry {Snapshot last;std::vector<Command> queued;bool begin(){return true;}void publish(const Snapshot&s){last=s;}bool receive(Command&command){if(queued.empty())return false;command=queued.front();queued.erase(queued.begin());return true;}}
namespace live {
Tick pending;bool queued=false,healthy=false,busy=false,usdtAllowed=false;uint8_t connectionCount=2;uint16_t providerMask=3,currentBookMask=0,currentConvertedBookMask=0;
bool begin(){return true;}bool receive(Tick& tick){if(!queued)return false;tick=pending;queued=false;return true;}
Status status(){Status s;s.connections=healthy?connectionCount:0;s.providers=healthy?providerMask:0;s.currentBooks=healthy?currentBookMask:0;s.currentConvertedBooks=healthy?currentConvertedBookMask:0;return s;}bool fresh(uint32_t){return healthy;}
bool pauseAndWait(uint32_t){return true;}void resume(){}void httpBusy(bool value){busy=value;}void allowUsdt(bool value){usdtAllowed=value;}
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
  wifi.loop(testMillis);schedule(testMillis);assert(market::sent&&streamWarmupStarted&&market::outstanding.kind==market::Kind::Quote);
  unsigned firstCalls=market::calls;testMillis+=4999;schedule(testMillis);assert(market::calls==firstCalls);
  testMillis+=1;schedule(testMillis);assert(market::sent&&market::outstanding.kind==market::Kind::Quote&&!btc::kSources[market::outstanding.source].usdt);
  assert(wifi.setupState()==btc::WifiSetupState::Connected);testMillis+=25;packets.clear();draw(testMillis);
  assert(display::startupPending());
  market::complete();processResult(testMillis);assert(guard.price==83300&&!guard.confirmed()&&needConfirmation);
  assert(nextQuoteAt==testMillis);
  packets.clear();refreshNixies(testMillis);draw(testMillis);unsigned liveDigits=0;for(const auto&p:packets)if(p.bus==VSPI)++liveDigits;assert(liveDigits==6);
  testMillis+=8001;wifi.loop(testMillis);assert(!wifi.portalActive());packets.clear();draw(testMillis);assert(!showingSetup&&display::startupAnimating()&&nextDrawAt==testMillis+config::kStartupFrameMs);
  assert(display::startupAnimating()); // A first real candle starts the reveal immediately.
  schedule(testMillis);assert(!market::sent); // Background confirmation must not cut off stream startup.
  testMillis+=27000;schedule(testMillis);assert(market::sent); // Stream grace is bounded.
  // An independent live quote confirms the initial source in the background.
  live::pending.quote.price=83300;live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.receivedAt=testMillis;live::pending.source=1;live::queued=true;processLive(testMillis);
  assert(guard.confirmed()&&!needConfirmation);live::healthy=true;market::complete();processResult(testMillis);
  schedule(testMillis);assert(market::outstanding.kind==market::Kind::Candles);
  unsigned before=market::calls;
  adc[35]=142;adc[34]=1328;readDials(testMillis);testMillis+=151;readDials(testMillis);
  assert(refreshDial.stable==4&&timelineDial.stable==1);schedule(testMillis);assert(market::calls==before);
  Serial.input="dials\n";serialCommands(testMillis);assert(Serial.log.find("Frequency JSEL2 GPIO35=142 mV; range JSEL1 GPIO34=1328 mV")!=std::string::npos);
  Serial.input="ticks on\nticks on\n";serialCommands(testMillis);assert(logNixies);
  Serial.input="ticks off\nticks off\n";serialCommands(testMillis);assert(!logNixies);
  market::complete();processResult(testMillis);
  display::begin();packets.clear();draw(testMillis);assert(display::startupAnimating()&&nextDrawAt==testMillis+config::kStartupFrameMs);
  testMillis+=273*config::kStartupCellMs;draw(testMillis);assert(display::startupAnimating());
  testMillis+=273*config::kStartupCellMs;draw(testMillis);assert(!display::startupAnimating()&&nextDrawAt>testMillis&&nextDrawAt<=testMillis+1000);
  liveReady=false;live::healthy=false; // Exercise REST fallback independently of stream suppression.
  for(int setting=0;setting<5;++setting){
    testMillis+=6000;refreshDial.stable=setting;fx.usd=1;fx.receivedAt=testMillis;nextQuoteAt=testMillis;
    for(uint32_t&deadline:nextHistoryAt)deadline=testMillis+1000000;
    schedule(testMillis);assert(market::outstanding.kind==market::Kind::Quote);assert(nextQuoteAt==testMillis+config::kRefreshMs[setting]);
    market::complete();processResult(testMillis);unsigned calls=market::calls;testMillis+=std::min<uint32_t>(100,config::kRefreshMs[setting]-1);schedule(testMillis);assert(market::calls==calls);
  }
  refreshDial.stable=4;testMillis+=61000;nextQuoteAt=testMillis+200000;fx.receivedAt=testMillis-61000;
  schedule(testMillis);assert(market::outstanding.kind==market::Kind::Fx);double oldPrice=guard.price;uint32_t oldReceived=lastQuoteReceived;
  market::complete();market::queued.quote.price=90000;processResult(testMillis);assert(guard.price==oldPrice&&lastQuoteReceived==oldReceived);
  display::price(guard.price);packets.clear();lastQuoteReceived=testMillis-btc::staleAfterMs(config::kRefreshMs[4])-1;draw(testMillis);assert(!packets.empty());
  // A large quote waits for another provider rather than lighting an outlier.
  market::outstanding={market::Kind::Quote,3,1,uint32_t(testEpoch)};market::sent=true;inflight=true;market::complete();market::queued.quote.price=150000;processResult(testMillis);assert(guard.price==oldPrice);
}
void cacheRuntimeTests(){
  liveReady=true;live::healthy=true;needDifferentQuote=false;
  testEpoch=1800000000;testMillis+=6000;timelineDial.stable=4;refreshDial.stable=4;fx.usd=1;fx.receivedAt=testMillis;
  guard.price=83300;guard.acceptedAt=testMillis;lastQuoteReceived=testMillis;lastPriceEpoch=uint32_t(testEpoch);
  for(auto& state:rotation.states){state.availableAt=testMillis;state.failures=0;}
  candleSeries[4]={};uint64_t now=chartNowMs();uint32_t current=btc::candleBucket(now,4);
  for(unsigned col=0;col<21;++col)assert(candleSeries[4].add(btc::candleStart(current-20+col,4),83300+col,4,now));
  markCandleDirty(4,testMillis);nextCandleSave[4]=testMillis;for(unsigned iteration=0;iteration<5;++iteration){saveHistory(testMillis);if(market::sent){market::complete();processResult(testMillis);}}
  assert(candleSavedTime[4]/1000==uint64_t(testEpoch)&&!candleDirty[4]);candleBackfilled[4]=true;
  unsigned writes=fsMock().writes;saveHistory(testMillis);assert(fsMock().writes==writes);
  nextQuoteAt=testMillis+1800000;nextHistoryAt[4]=testMillis;unsigned calls=market::calls;schedule(testMillis);assert(market::calls==calls);
  adc[34]=493;readDials(testMillis);testMillis+=151;readDials(testMillis);assert(timelineDial.stable==3);
  adc[34]=142;testMillis+=25;readDials(testMillis);testMillis+=151;readDials(testMillis);assert(timelineDial.stable==4);
  nextQuoteAt=testMillis+1800000;fx.receivedAt=testMillis;schedule(testMillis);assert(market::calls==calls);
  // A restarted device draws the dated full OHLC snapshot before SNTP.
  btc::CandleSeries restored[5];btc::CandleScale axes[5];CandleStore reboot;assert(reboot.begin(restored,axes));
  assert(!memcmp(&restored[4],&candleSeries[4],sizeof(restored[4])));candleScales[4]=axes[4];
  testEpoch=0;guard=btc::PriceGuard{};lastPriceEpoch=0;nextDrawAt=testMillis;display::begin();
  display::message("SYNC","CLOCK",testMillis);packets.clear();draw(testMillis);
  for(const auto& p:packets)if(p.bus==VSPI)for(int digit=1;digit<16;++digit)assert(p.bytes[digit]==0x80);
  assert(display::startupAnimating());testMillis+=273*config::kStartupCellMs;draw(testMillis);
  testMillis+=273*config::kStartupCellMs;packets.clear();draw(testMillis);assert(!display::startupAnimating());
  auto frame=btc::makeCandles(candleSeries[4],candleScales[4],4,candleStore.savedTimeMs(4));auto chart=frame.plot();
  uint8_t registers[6][8]={};unsigned frames=0;for(const auto& p:packets)if(p.bus==HSPI){++frames;for(unsigned slot=0;slot<6;++slot)registers[config::kMaxShiftOrder[slot]][p.bytes[slot*2]-1]=p.bytes[slot*2+1];}
  assert(frames==8);for(unsigned row=0;row<13;++row)for(unsigned col=0;col<21;++col){auto p=led_matrix::mapPixel(row,col);assert(bool(registers[p.driver][p.digit]&(1U<<p.segmentBit))==btc::plotPixel(chart,row,col));}
  testEpoch=1800000120;guard=btc::PriceGuard{};guard.accept(83300,0,testMillis);guard.accept(83300,1,testMillis);lastPriceEpoch=uint32_t(testEpoch);
  // A lost stream requests dated transactions; cached old bars remain visible.
  live::healthy=false;liveReady=false;nextHistoryAt[4]=testMillis;nextQuoteAt=testMillis+100000;for(auto& state:rotation.states)state.availableAt=testMillis;
  schedule(testMillis);assert(market::sent&&market::outstanding.kind==market::Kind::Candles);
  market::complete();processResult(testMillis);assert(candleSeries[4].count(4,chartNowMs())==21);
  liveReady=true;live::healthy=true;
  std::cout<<"PASS: actual OHLC runtime persistence, instant range recall, no redundant fetches, outage backfill and saved candle chart before SNTP\n";
}

void resetCandleRuntime(){
  assert(wifi.online()&&!wifi.portalActive()&&!market::sent&&!inflight);
  testMillis+=6000;testEpoch=1800000180;
  refreshDial=btc::Dial(4);timelineDial=btc::Dial(2);refreshDial.initialized=timelineDial.initialized=true;
  adc[35]=config::kRefreshMv[4];adc[34]=config::kTimelineMv[2];
  guard=btc::PriceGuard{};assert(guard.accept(83300,0,testMillis)&&guard.accept(83300,1,testMillis));
  fx.usd=1;fx.receivedAt=testMillis;quotes.clear();quotes.put(83300,0,testMillis,uint32_t(testEpoch));
  lastQuoteReceived=testMillis;lastPriceEpoch=uint32_t(testEpoch);lastChartTickAt=testMillis;chartContinuousSince=chartNowMs();
  chartSource=255;chartQuoteReceived=0;shownQuoteKind=lastAcceptedKind=btc::QuoteKind::LastTrade;
  priceSequence.clear();shownPrice=0;priceReversals=selectionHolds=0;
  networkReady=liveReady=streamWarmupStarted=true;live::healthy=true;live::connectionCount=2;live::providerMask=3;live::currentBookMask=live::currentConvertedBookMask=0;live::busy=false;live::queued=false;live::pending={};
  streamWarmupUntil=testMillis;nextQuoteAt=testMillis+100000;needDifferentQuote=needConfirmation=false;differentQuoteSince=0;
  forceOtaPending=ota::due=false;ota::validBoot=true;matrixDiagnostics=false;testEndsAt=tubeTestEndsAt=0;
  market::ready=false;
  for(unsigned w=0;w<5;++w){candleSeries[w]={};candleScales[w]={};candleDirty[w]=candleBackfilled[w]=false;
    candleReferencePrice[w]=0;nextCandleSave[w]=0;nextHistoryAt[w]=testMillis;historyErrors[w]=0;historySource[w]=2;}
  for(auto& state:rotation.states){state.availableAt=testMillis;state.failures=0;}
}
void seedRuntimeBars(unsigned window){
  uint64_t now=chartNowMs();uint32_t current=btc::candleBucket(now,window);
  for(unsigned col=0;col<21;++col){
    uint64_t stamp=btc::candleStart(current-20+col,window);double price=83300+col*.5;
    assert(candleSeries[window].add(stamp,price,window,now));
    if(stamp+3<=now){assert(candleSeries[window].add(stamp+1,price+1,window,now));
      assert(candleSeries[window].add(stamp+2,price-1,window,now));assert(candleSeries[window].add(stamp+3,price+.25,window,now));}
  }
  assert(candleSeries[window].count(window,now)==21&&candleScales[window].update(candleSeries[window],window,now));
  candleReferencePrice[window]=candleSeries[window].latestCloseCents(window);
}
void assertSameScales(const btc::CandleScale (&expected)[5],const btc::CandleScale (&actual)[5]){
  for(unsigned w=0;w<5;++w){assert(expected[w].low==actual[w].low&&expected[w].step==actual[w].step&&expected[w].smoothedSpan==actual[w].smoothedSpan);
    assert(expected[w].lastEpoch==actual[w].lastEpoch&&expected[w].lastPan==actual[w].lastPan&&expected[w].lastResize==actual[w].lastResize);}
}
void selectRuntimeRange(unsigned window){
  adc[34]=config::kTimelineMv[window];nextDialsAt=testMillis;readDials(testMillis);
  testMillis+=config::kDebounceMs+1;readDials(testMillis);assert(timelineDial.stable==int(window));
  nextDrawAt=testMillis;draw(testMillis);
}
void sharedCandleRuntimeTests(){
  resetCandleRuntime();for(unsigned w=0;w<5;++w){seedRuntimeBars(w);candleBackfilled[w]=true;}
  auto original=candleSeries[2];auto originalScale=candleScales[2];
  auto originalFrame=btc::makeCandles(original,originalScale,2,chartNowMs());
  uint32_t forming=btc::candleBucket(chartNowMs(),2);unsigned calls=market::calls;
  selectRuntimeRange(3);schedule(testMillis);assert(market::calls==calls);
  // Market time and current candles progress while another range is selected.
  ++testEpoch;testMillis+=1000;btc::Quote tick;tick.price=83310.4;tick.timestamp=uint32_t(testEpoch);
  acceptQuote(tick,0,testMillis,uint32_t(testEpoch),testMillis);
  selectRuntimeRange(2);schedule(testMillis);
  assert(market::calls==calls&&candleScales[2].low==originalScale.low&&candleScales[2].step==originalScale.step);
  for(const auto& bar:original.bars)if(bar.bucket!=forming)assert(!memcmp(&bar,&candleSeries[2].bars[bar.bucket%21],sizeof(bar)));
  assert(candleSeries[2].bars[forming%21].close==8331040);
  auto returned=btc::makeCandles(candleSeries[2],candleScales[2],2,chartNowMs());
  assert(returned.valid==0x1FFFFF&&chartColumns==0x1FFFFF&&chartPixels>=21);
  for(unsigned col=0;col<20;++col)assert(returned.bodies[col]==originalFrame.bodies[col]&&returned.wicks[col]==originalFrame.wicks[col]);

  resetCandleRuntime();schedule(testMillis);assert(market::sent&&market::outstanding.kind==market::Kind::Candles&&market::outstanding.window==2);
  calls=market::calls;selectRuntimeRange(3);schedule(testMillis);assert(market::calls==calls); // The request remains bound to its original range.
  tick.price=83298;tick.timestamp=uint32_t(testEpoch);acceptQuote(tick,0,testMillis,uint32_t(testEpoch),testMillis);
  market::complete();assert(market::queued.candleAttemptMask==0x1F&&market::queued.candleCoverageMask==0x1F);processResult(testMillis);
  assert(timelineDial.stable==3&&!inflight&&!market::sent);
  for(unsigned w=0;w<5;++w){assert(candleSeries[w].count(w,chartNowMs())==21&&candleBackfilled[w]);
    auto current=btc::candleBucket(chartNowMs(),w);assert(candleSeries[w].bars[current%21].close==8329800);}
  for(unsigned w=0;w<5;++w){selectRuntimeRange(w);schedule(testMillis);assert(market::calls==calls&&chartColumns==0x1FFFFF&&chartPixels>=21);}

  // Even an inconsistent success with optimistic coverage cannot replace a
  // retained chart with empty observations or suppress the repair request.
  resetCandleRuntime();seedRuntimeBars(2);original=candleSeries[2];schedule(testMillis);assert(market::sent);
  market::complete();for(auto& series:market::queued.candles)series={};market::queued.quote={};
  market::queued.candleCoverageMask=0x1F;processResult(testMillis);
  assert(!memcmp(&original,&candleSeries[2],sizeof(original))&&historyErrors[2]==1&&historySource[2]==0);
  for(bool covered:candleBackfilled)assert(!covered);
  calls=market::calls;testMillis+=1001;schedule(testMillis);
  assert(market::calls==calls+1&&market::outstanding.kind==market::Kind::Candles&&market::outstanding.source==0&&market::outstanding.window==2);
  market::complete();processResult(testMillis);assert(!candleBackfilled[2]); // Coinbase repair remains partial.

  resetCandleRuntime();for(unsigned w=0;w<5;++w){seedRuntimeBars(w);candleBackfilled[w]=true;}
  btc::CandleSeries retained[5];std::copy(candleSeries,candleSeries+5,retained);
  market::outstanding={market::Kind::Candles,0,3,uint32_t(testEpoch)};market::sent=inflight=true;market::complete();
  for(auto& series:market::queued.candles)series={};market::queued.candleCoverageMask=0;
  assert(market::queued.candles[2].add(chartNowMs(),83310.5,2,chartNowMs()));processResult(testMillis);
  for(unsigned w=0;w<5;++w){assert(candleBackfilled[w]&&candleSeries[w].count(w,chartNowMs())==21);
    uint32_t current=btc::candleBucket(chartNowMs(),w);
    for(const auto& bar:retained[w].bars)if(w!=2||bar.bucket!=current)assert(!memcmp(&bar,&candleSeries[w].bars[bar.bucket%21],sizeof(bar)));}
  assert(candleSeries[2].bars[btc::candleBucket(chartNowMs(),2)%21].close==8331050);
  std::cout<<"PASS: debounced 5m/30m/5m recall retains closed candles, scale and pixels; late shared replies merge all five caches; coverage avoids redundant downloads; empty/partial repairs preserve prior bars and coverage\n";
}
void liveFxRuntimeTests(){
  resetCandleRuntime();for(unsigned w=0;w<5;++w)seedRuntimeBars(w);
  btc::CandleSeries before[5];std::copy(candleSeries,candleSeries+5,before);auto priorBook=quotes;auto priorGuard=guard;
  uint32_t accepted=acceptedQuotes,received=lastQuoteReceived,chartTick=lastChartTickAt;
  fx.usd=NAN;live::usdtAllowed=false;live::pending={};live::pending.source=1;live::pending.receivedAt=testMillis-40;
  live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.fx=.998;live::queued=true;processLive(testMillis);
  assert(fx.usd==.998&&fx.receivedAt==testMillis-40&&live::usdtAllowed);
  auto unchanged=[&](){assert(acceptedQuotes==accepted&&lastQuoteReceived==received&&lastChartTickAt==chartTick);
    assert(!memcmp(&quotes,&priorBook,sizeof(quotes))&&!memcmp(&guard,&priorGuard,sizeof(guard)));
    assert(!memcmp(before,candleSeries,sizeof(before)));};unchanged();
  for(unsigned invalid=0;invalid<7;++invalid){
    fx.usd=NAN;live::usdtAllowed=false;live::pending={};live::pending.source=1;live::pending.receivedAt=testMillis;
    live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.fx=1;
    if(invalid==0)live::pending.fx=.49;
    if(invalid==1)live::pending.fx=1.51;
    if(invalid==2)live::pending.fx=NAN;
    if(invalid==3)live::pending.source=0;
    if(invalid==4)live::pending.quote.timestamp=uint32_t(testEpoch)-config::kQuoteMaxAgeSeconds-1;
    if(invalid==5)live::pending.receivedAt=testMillis-5001;
    if(invalid==6)live::pending.quote.price=83310;
    live::queued=true;processLive(testMillis);assert(std::isnan(fx.usd)&&!live::usdtAllowed);unchanged();
  }
  fx.usd=1;fx.receivedAt=testMillis-config::kFxMaxAgeMs-1;processLive(testMillis);assert(!live::usdtAllowed);
  fx.receivedAt=testMillis;live::pending={};processLive(testMillis);assert(live::usdtAllowed);
  std::cout<<"PASS: fresh Kraken FX-only ticks update conversion and USDT gating without price/chart mutation; invalid, mixed, stale and expired conversion never enables USDT\n";
}
void secondStreamWarmupRuntimeTests(){
  resetCandleRuntime();live::connectionCount=1;needDifferentQuote=true;differentQuoteSince=testMillis-std::max<uint32_t>(5000,refreshPeriod());
  market::outstanding={market::Kind::Quote,0,2,uint32_t(testEpoch)};market::sent=inflight=true;market::complete();processResult(testMillis);
  uint32_t start=testMillis;assert(streamWarmupUntil==start+35000);unsigned calls=market::calls;
  for(unsigned elapsed:{0U,10000U,34999U}){testMillis=start+elapsed;nextQuoteAt=nextHistoryAt[2]=testMillis;schedule(testMillis);assert(market::calls==calls);}
  // Once both feeds are established, the second-stream grace must not hold
  // ordinary HTTP work until a stale timer expires.
  live::connectionCount=2;nextHistoryAt[2]=testMillis+100000;nextQuoteAt=testMillis;schedule(testMillis);
  assert(market::calls==calls+1&&market::outstanding.kind==market::Kind::Quote);market::complete();processResult(testMillis);
  live::connectionCount=1;streamWarmupUntil=testMillis+35000;nextQuoteAt=testMillis;needDifferentQuote=true;
  calls=market::calls;schedule(testMillis);assert(market::calls==calls);
  forceOtaPending=true;schedule(testMillis);assert(market::calls==calls+1&&market::outstanding.kind==market::Kind::Ota&&!forceOtaPending);
  market::complete();processResult(testMillis);live::connectionCount=2;needDifferentQuote=false;fx.receivedAt=testMillis;
  std::cout<<"PASS: one fresh stream gets 35 seconds for its second feed; two established feeds bypass that grace; forced signed OTA remains available during reconnect\n";
}

void soak(){
  liveReady=false;live::healthy=false;quotes.clear();priceSequence.clear(); // REST-only availability/cooldown soak.
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
    for(unsigned digit=1;digit<16;++digit){uint8_t expected=(digit==unsigned(digits[module]?digits[module]:10))?0xC0:0x80;
      if(p.bytes[digit]!=expected)std::cerr<<"SPI price mismatch: expected="<<value<<" shown="<<shownPrice<<" digits="<<shownDigits<<" at="<<testMillis<<" sequence="<<priceSequence.latest()<<" module="<<module<<" field="<<digit<<"\n";
      assert(p.bytes[digit]==expected);}}
}
void nixieCadenceTests(){
  assert(wifi.online()&&!wifi.portalActive());
  uint32_t initial=testMillis;
  for(unsigned setting=0;setting<5;++setting){
    uint32_t period=config::kRefreshMs[setting],start=initial+setting*10000000;
    testMillis=start;refreshDial.stable=setting;guard=btc::PriceGuard{};guard.accept(83300,0,start);assert(guard.accept(83300,1,start));
    quotes.clear();priceSequence.clear();shownPrice=0;lastQuoteReceived=start;nextNixieAt=start;needConfirmation=false;
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
    packets.clear();testMillis=start+2*period;refreshNixies(testMillis);assertPriceFrame(83342);assert(nixieFrames==frames+2); // Every cadence writes the real rounded value, even unchanged.
    inflight=false;market::sent=false;needDifferentQuote=false;
  }
  // A dial change takes effect immediately even with a network request active.
  testMillis+=100;refreshDial.stable=4;refreshDial.candidate=-1;nextDialsAt=testMillis;
  inflight=true;adc[35]=1774;readDials(testMillis);testMillis+=151;readDials(testMillis);assert(refreshDial.stable==0&&nextNixieAt==testMillis);
  lastQuoteReceived=testMillis;packets.clear();refreshNixies(testMillis);assertPriceFrame(83342);assert(nextNixieAt==testMillis+100);inflight=false;refreshDial.stable=1;
  // Once a stream is fresh, REST traffic is only needed for confirmation or fallback.
  needConfirmation=false;needDifferentQuote=false;live::healthy=true;fx.usd=1;fx.receivedAt=testMillis;nextQuoteAt=testMillis;for(auto&deadline:nextHistoryAt)deadline=testMillis+100000;
  unsigned calls=market::calls;schedule(testMillis);assert(market::calls==calls);
  live::healthy=false;for(auto&state:rotation.states)state.availableAt=testMillis;schedule(testMillis);assert(market::calls==calls+1&&live::busy);
  market::complete();processResult(testMillis);assert(!live::busy);
  // Cadence and stale blanking remain correct across millis rollover.
  testMillis=UINT32_MAX-249;lastQuoteReceived=fx.receivedAt=testMillis;shownPrice=0;priceSequence.clear();packets.clear();refreshNixies(testMillis);assertPriceFrame(guard.price);
  packets.clear();testMillis=249;refreshNixies(testMillis);assert(packets.empty());
  testMillis=250;refreshNixies(testMillis);assertPriceFrame(guard.price);
  packets.clear();testMillis+=btc::staleAfterMs(500)+1;refreshNixies(testMillis);assert(shownPrice==0&&packets.size()==6);
  for(auto&p:packets)for(unsigned digit=1;digit<16;++digit)assert(p.bytes[digit]==0x80);
  lastQuoteReceived=testMillis;packets.clear();refreshNixies(testMillis);assertPriceFrame(guard.price);
  // A late loop emits one current frame, without fake catch-up frames or timer drift.
  uint32_t deadline=nextNixieAt;testMillis=deadline+1005;lastQuoteReceived=testMillis;packets.clear();refreshNixies(testMillis);assertPriceFrame(guard.price);assert(nextNixieAt==deadline+1500);
  // Confirming the default detent must not emit an early same-frequency frame.
  deadline=nextNixieAt;refreshDial.initialized=false;refreshDial.candidate=-1;
  nextDialsAt=testMillis;adc[35]=1328;readDials(testMillis);testMillis+=151;readDials(testMillis);
  assert(refreshDial.initialized&&refreshDial.stable==1&&nextNixieAt==deadline);
  std::cout<<"PASS: actual Nixie SPI frames at all five selected intervals, unchanged digits, newest stream values, blocked history/TLS, immediate dial changes, REST fallback, outage blanking and millis rollover\n";
}
void quoteSelectionRuntimeTests(){
  testMillis+=6000;refreshDial.stable=1;guard=btc::PriceGuard{};quotes.clear();priceSequence.clear();shownPrice=0;
  auto tick=[&](uint8_t provider,double value){live::pending.quote.price=value;live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.receivedAt=testMillis;live::pending.source=provider;live::queued=true;processLive(testMillis);};
  tick(0,80010.25);tick(1,80017.6);tick(0,80010.25);
  for(unsigned i=0;i<100;++i)tick(1,80017.6);
  packets.clear();refreshNixies(testMillis);assertPriceFrame(80018);
  testMillis+=500;tick(1,80017.6);packets.clear();refreshNixies(testMillis);assertPriceFrame(80010);
  assert(shownSource==0&&shownPrice==80010.25&&guard.price==80017.6&&lastPriceEpoch==uint32_t(testEpoch));
  testMillis+=500;tick(1,80017.6);packets.clear();refreshNixies(testMillis);assertPriceFrame(80010); // Hold instead of returning to the prior distinct number.
  assert(priceReversals==0&&selectionHolds==1);
  testMillis+=500;tick(2,80024.2);packets.clear();refreshNixies(testMillis);assertPriceFrame(80024); // A third genuine number breaks the two-value hold.
  // Expired alternative quotes cannot be used to create apparent movement.
  testMillis+=5000;tick(1,80017.6);packets.clear();refreshNixies(testMillis);assertPriceFrame(80018);
  assert(guard.price==80017.6&&shownPrice==80017.6&&lastPriceEpoch==uint32_t(testEpoch));
  quotes.clear();
  std::cout<<"PASS: actual runtime/SPI source rotation survives burst overwrite, holds through two-number scarcity without reversing, resumes on a third real number, and never invents prices or renews expired timestamps\n";
}
void sequencedPriceRuntimeTests(){
  const double realQuotes[4]={83111.25,83222.25,83333.25,83444.25};
  for(unsigned frequency=0;frequency<5;++frequency){
    resetCandleRuntime();refreshDial.stable=frequency;quotes.clear();shownPrice=0;nextNixieAt=testMillis;
    for(unsigned w=0;w<5;++w)seedRuntimeBars(w);
    uint32_t start=testMillis,period=config::kRefreshMs[frequency],frames=nixieFrames;
    time_t initialEpoch=testEpoch;uint32_t last=0,previousDistinct=0;std::set<unsigned> sources;
    for(unsigned frame=0;frame<9;++frame){
      if(frame){testMillis=start+frame*period-1;packets.clear();refreshNixies(testMillis);assert(packets.empty()&&nixieFrames==frames+frame);}
      testMillis=start+frame*period;testEpoch=initialEpoch+frame*period/1000;uint32_t accepted=acceptedQuotes;
      for(unsigned source=0;source<4;++source){
        live::pending={};live::pending.source=source;live::pending.receivedAt=testMillis;
        live::pending.quote.price=realQuotes[source];live::pending.quote.timestamp=uint32_t(testEpoch);
        live::queued=true;processLive(testMillis);
      }
      assert(acceptedQuotes==accepted+4);btc::CandleSeries before[5];btc::CandleScale axes[5];
      std::copy(candleSeries,candleSeries+5,before);std::copy(candleScales,candleScales+5,axes);
      packets.clear();refreshNixies(testMillis);assertPriceFrame(shownDigits);
      assert(shownSource<4&&shownPrice==realQuotes[shownSource]&&shownDigits==uint32_t(std::floor(realQuotes[shownSource]+.5)));
      assert(shownQuoteReceived==testMillis&&shownQuoteTimestamp==uint32_t(testEpoch)&&shownQuoteKind==btc::QuoteKind::LastTrade);
      assert(nixieFrames==frames+frame+1&&nextNixieAt==testMillis+period&&acceptedQuotes==accepted+4);
      assert(!memcmp(before,candleSeries,sizeof(before)));assertSameScales(axes,candleScales);
      if(last){assert(shownDigits!=last&&shownDigits!=previousDistinct);previousDistinct=last;}
      last=shownDigits;sources.insert(shownSource);if(frame==3)assert(sources.size()==4); // Exhaust unused genuine choices before recycling.
    }
    assert(priceTransitions==8&&priceChanges==8&&multiDigitChanges==8&&priceReversals==0&&selectionHolds==0);
    assert(minimumFrameGap==period&&maximumFrameGap==period&&sources.size()==4);
  }
  std::cout<<"PASS: all five dial cadences render only four validated real quotes, visit unused choices, forbid immediate reversals, preserve exact publication times and leave quote counters/candles/scales untouched by selection\n";
}
void convertedBookRuntimeTests(){
  resetCandleRuntime();refreshDial.stable=0;live::providerMask=(1U<<0)|(1U<<4);live::currentBookMask=live::currentConvertedBookMask=1U<<4;
  for(unsigned w=0;w<5;++w){seedRuntimeBars(w);nextHistoryAt[w]=testMillis+120000;}
  quotes.clear();shownPrice=0;nextNixieAt=nextQuoteAt=testMillis;
  uint32_t start=testMillis,originalEpoch=uint32_t(testEpoch),calls=market::calls;const double converted=85500.0*.999;
  fx.usd=1.2; // A source-4 converted quote is already USD; unrelated global FX must not multiply it again.
  auto incoming=[&](uint8_t source,double price,btc::QuoteKind kind){
    live::pending={};live::pending.source=source;live::pending.receivedAt=testMillis;
    live::pending.quote.price=price;live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.quote.kind=kind;
    live::queued=true;processLive(testMillis);
  };
  incoming(0,83310.4,btc::QuoteKind::LastTrade);packets.clear();refreshNixies(testMillis);assertPriceFrame(83310);
  testMillis=start+100;incoming(4,84567.25,btc::QuoteKind::BboMidpoint);packets.clear();refreshNixies(testMillis);assertPriceFrame(84567);
  btc::CandleSeries beforeConversion[5];btc::CandleScale axes[5];std::copy(candleSeries,candleSeries+5,beforeConversion);std::copy(candleScales,candleScales+5,axes);
  uint32_t chartReceipt=chartQuoteReceived,chartTick=lastChartTickAt;uint64_t continuous=chartContinuousSince;
  testMillis=start+200;incoming(4,converted,btc::QuoteKind::ConvertedBboMidpoint);
  assert(guard.price==converted&&lastAcceptedKind==btc::QuoteKind::ConvertedBboMidpoint);
  assert(chartReceipt==chartQuoteReceived&&chartTick==lastChartTickAt&&continuous==chartContinuousSince&&!memcmp(beforeConversion,candleSeries,sizeof(beforeConversion)));
  assertSameScales(axes,candleScales);packets.clear();refreshNixies(testMillis);assertPriceFrame(85415);assert(shownQuoteKind==btc::QuoteKind::ConvertedBboMidpoint);
  uint32_t accepted=acceptedQuotes,last=shownDigits,prior=84567;std::set<unsigned> kinds;
  for(unsigned frame=3;frame<=302;++frame){
    testMillis=start+frame*100;testEpoch=originalEpoch+frame/10;incoming(0,83310.4,btc::QuoteKind::LastTrade);
    btc::CandleSeries before[5];std::copy(candleSeries,candleSeries+5,before);uint32_t realAccepted=acceptedQuotes;
    packets.clear();refreshNixies(testMillis);assertPriceFrame(shownDigits);
    assert(shownDigits!=last&&shownDigits!=prior&&!needDifferentQuote);prior=last;last=shownDigits;
    assert(realAccepted==accepted+frame-2&&acceptedQuotes==realAccepted&&!memcmp(before,candleSeries,sizeof(before))&&chartSource==0);
    kinds.insert(unsigned(shownQuoteKind));
    if(shownQuoteKind==btc::QuoteKind::LastTrade)assert(shownSource==0&&shownPrice==83310.4&&shownQuoteReceived==testMillis&&shownQuoteTimestamp==uint32_t(testEpoch));
    else if(shownQuoteKind==btc::QuoteKind::BboMidpoint)assert(shownSource==4&&shownPrice==84567.25&&shownQuoteReceived==start+100&&shownQuoteTimestamp==originalEpoch);
    else assert(shownQuoteKind==btc::QuoteKind::ConvertedBboMidpoint&&shownSource==4&&shownPrice==converted&&shownQuoteReceived==start+200&&shownQuoteTimestamp==originalEpoch);
    for(unsigned w=0;w<5;++w)for(const auto& bar:candleSeries[w].bars)if(bar.bucket)assert(bar.high<=8331100);
    schedule(testMillis);assert(market::calls==calls&&!inflight&&!live::busy);
  }
  assert(kinds.size()==3&&priceTransitions==302&&priceChanges==302&&multiDigitChanges==302&&priceReversals==0&&selectionHolds==0&&minimumFrameGap==100&&maximumFrameGap==100);
  nextTelemetryAt=testMillis;publishTelemetry(testMillis);assert(telemetry::last.currentBooks==16&&telemetry::last.currentConvertedBooks==16&&telemetry::last.chartSource==0);
  // Even without a canonical venue, an accepted conversion is display-only.
  // Rejecting that conversion must not revoke the independent native USD book.
  live::providerMask=1U<<4;live::connectionCount=1;testMillis=start+61000;testEpoch=originalEpoch+61;
  std::copy(candleSeries,candleSeries+5,beforeConversion);chartReceipt=chartQuoteReceived;chartTick=lastChartTickAt;continuous=chartContinuousSince;
  incoming(4,converted+.25,btc::QuoteKind::ConvertedBboMidpoint);
  assert(chartReceipt==chartQuoteReceived&&chartTick==lastChartTickAt&&continuous==chartContinuousSince&&!memcmp(beforeConversion,candleSeries,sizeof(beforeConversion)));
  uint32_t acceptedBeforeReject=acceptedQuotes;incoming(4,150000,btc::QuoteKind::ConvertedBboMidpoint);assert(acceptedQuotes==acceptedBeforeReject&&needConfirmation);
  btc::DisplayQuote current;assert(quotes.choose(testMillis,83310,0,guard.price,true,current,16,nullptr,0,16)&&current.kind==btc::QuoteKind::BboMidpoint&&current.receivedAt==start+100);
  live::currentConvertedBookMask=live::currentBookMask=0;live::providerMask=3;live::connectionCount=2;live::pending={};
  std::cout<<"PASS: three genuine Coinbase/native-Bitfinex/converted-Bitfinex values change at all 302 actual 100 ms deadlines with zero immediate reversals or HTTP, distinct original publication fields, no double FX/counter inflation, and no converted OHLC observations even during a canonical outage\n";
}
void canonicalChartRuntimeTests(){
  resetCandleRuntime();refreshDial.stable=0;for(unsigned w=0;w<5;++w)seedRuntimeBars(w);
  live::providerMask=(1U<<0)|(1U<<4);quotes.clear();shownPrice=0;nextNixieAt=testMillis;
  auto tick=[&](uint8_t source,double price,btc::QuoteKind kind){
    live::pending={};live::pending.source=source;live::pending.receivedAt=testMillis;
    live::pending.quote.price=price;live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.quote.kind=kind;
    live::queued=true;processLive(testMillis);
  };
  tick(0,83310.4,btc::QuoteKind::LastTrade);assert(chartSource==0&&chartQuoteReceived==testMillis);
  packets.clear();refreshNixies(testMillis);assertPriceFrame(83310);assert(shownQuoteKind==btc::QuoteKind::LastTrade);
  uint32_t canonicalAt=lastChartTickAt,start=testMillis,accepted=acceptedQuotes;time_t epochStart=testEpoch;
  btc::CandleSeries canonical[5];btc::CandleScale axes[5];
  std::copy(candleSeries,candleSeries+5,canonical);std::copy(candleScales,candleScales+5,axes);
  uint32_t references[5];std::copy(candleReferencePrice,candleReferencePrice+5,references);
  // Record actual flash snapshots before receiving the premium quote. Restore
  // the test filesystem afterward so its local generations do not affect the
  // application's separate CandleStore bookkeeping in subsequent suites.
  auto priorFiles=fsMock().files;btc::CandleSeries saved[5];btc::CandleScale savedAxes[5];CandleStore checkpoint;
  assert(checkpoint.begin(saved,savedAxes));
  for(unsigned w=0;w<5;++w){assert(checkpoint.save(w,canonical[w],axes[w],chartNowMs()));candleDirty[w]=false;nextCandleSave[w]=testMillis+30000;}
  auto committed=fsMock().files;
  testMillis=start+100;tick(4,84567,btc::QuoteKind::BboMidpoint);
  assert(acceptedQuotes==accepted+1&&guard.price==84567&&lastChartTickAt==canonicalAt);
  packets.clear();refreshNixies(testMillis);assertPriceFrame(84567);assert(shownSource==4&&multiDigitChanges==1&&shownQuoteKind==btc::QuoteKind::BboMidpoint);
  nextTelemetryAt=testMillis;publishTelemetry(testMillis);
  assert(telemetry::last.chartSource==0&&telemetry::last.chartQuoteAge==100&&telemetry::last.quoteKind==btc::QuoteKind::BboMidpoint);
  for(unsigned i=2;i<=4;++i){testMillis=start+i*100;tick(4,84567+i,btc::QuoteKind::BboMidpoint);}
  assert(!memcmp(canonical,candleSeries,sizeof(canonical)));assertSameScales(axes,candleScales);
  assert(!memcmp(references,candleReferencePrice,sizeof(references))&&lastChartTickAt==canonicalAt);
  for(bool dirty:candleDirty)assert(!dirty);
  saveHistory(testMillis);assert(!inflight&&fsMock().files==committed);
  CandleStore reboot;assert(reboot.begin(saved,savedAxes));
  assert(!memcmp(canonical,saved,sizeof(canonical)));
  for(unsigned w=0;w<5;++w)assert(savedAxes[w].low==axes[w].low&&savedAxes[w].step==axes[w].step);
  fsMock().files=priorFiles;
  selectRuntimeRange(3);selectRuntimeRange(2);assert(chartColumns==0x1FFFFF&&chartPixels>=21);
  assert(!memcmp(canonical,candleSeries,sizeof(canonical))&&candleScales[2].low==axes[2].low&&candleScales[2].step==axes[2].step);

  // A connected canonical feed can be quiet without the other exchange's
  // premium creating false volatility in the chart during its 30-second grace.
  testMillis=start+29999;testEpoch=epochStart+29;tick(4,84570,btc::QuoteKind::BboMidpoint);
  assert(chartSource==0&&lastChartTickAt==canonicalAt&&!memcmp(canonical,candleSeries,sizeof(canonical)));
  live::providerMask=1U<<4;live::connectionCount=1;testMillis=start+31000;testEpoch=epochStart+31;
  tick(4,84572,btc::QuoteKind::BboMidpoint);assert(chartSource==4&&chartQuoteReceived==testMillis&&lastChartTickAt==testMillis);
  for(unsigned w=0;w<5;++w){uint32_t current=btc::candleBucket(chartNowMs(),w);assert(candleSeries[w].bars[current%21].close==8457200);
    for(const auto& old:canonical[w].bars)if(old.bucket+20>=current&&old.bucket<current)assert(!memcmp(&old,&candleSeries[w].bars[old.bucket%21],sizeof(old)));}
  // Fresh Coinbase immediately restores the chart, even though the fallback
  // remains fresh and continues to offer a different real price for the tubes.
  live::providerMask=(1U<<0)|(1U<<4);live::connectionCount=2;testMillis=start+31100;
  tick(0,83312,btc::QuoteKind::LastTrade);assert(chartSource==0&&chartQuoteReceived==testMillis&&lastChartTickAt==testMillis);
  for(unsigned w=0;w<5;++w){uint32_t current=btc::candleBucket(chartNowMs(),w);assert(candleSeries[w].bars[current%21].close==8331200);
    for(const auto& old:canonical[w].bars)if(old.bucket+20>=current&&old.bucket<current)assert(!memcmp(&old,&candleSeries[w].bars[old.bucket%21],sizeof(old)));}
  packets.clear();refreshNixies(testMillis);assertPriceFrame(83312);assert(shownSource==0&&shownQuoteKind==btc::QuoteKind::LastTrade);
  live::providerMask=3;live::pending={};
  std::cout<<"PASS: premium Bitfinex midpoint drives genuine 100 ms tube changes without altering canonical Coinbase OHLC, scale, saved caches or range recall; quiet-feed grace, outage fallback and immediate Coinbase recovery retain closed candles\n";
}
void quietCurrentBookRuntimeTests(){
  resetCandleRuntime();refreshDial.stable=0;live::providerMask=(1U<<0)|(1U<<4);live::currentBookMask=1U<<4;
  for(unsigned w=0;w<5;++w){seedRuntimeBars(w);nextHistoryAt[w]=testMillis+120000;}
  quotes.clear();shownPrice=0;nextNixieAt=nextQuoteAt=testMillis;
  uint32_t start=testMillis;time_t startEpoch=testEpoch;
  auto incoming=[&](uint8_t source,double price,btc::QuoteKind kind){
    live::pending={};live::pending.source=source;live::pending.receivedAt=testMillis;
    live::pending.quote.price=price;live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.quote.kind=kind;
    live::queued=true;processLive(testMillis);
  };
  incoming(0,83310.4,btc::QuoteKind::LastTrade);packets.clear();refreshNixies(testMillis);assertPriceFrame(83310);
  testMillis=start+100;incoming(4,84567,btc::QuoteKind::BboMidpoint);
  uint32_t publishedAt=testMillis,publishedEpoch=uint32_t(testEpoch),acceptedAtPublication=acceptedQuotes,calls=market::calls;
  packets.clear();refreshNixies(testMillis);assertPriceFrame(84567);
  for(unsigned frame=2;frame<=300;++frame){
    testMillis=start+frame*100;testEpoch=startEpoch+frame/10;incoming(0,83310.4,btc::QuoteKind::LastTrade);
    uint32_t actualAccepted=acceptedQuotes;btc::CandleSeries beforeRender[5];std::copy(candleSeries,candleSeries+5,beforeRender);
    packets.clear();refreshNixies(testMillis);assertPriceFrame(84567);
    assert(acceptedQuotes==actualAccepted&&!memcmp(beforeRender,candleSeries,sizeof(beforeRender)));
    assert(chartSource==0&&chartQuoteReceived==testMillis&&acceptedQuotes==acceptedAtPublication+frame-1);
    if(shownSource==4){assert(shownQuoteKind==btc::QuoteKind::BboMidpoint&&shownQuoteReceived==publishedAt);
      btc::DisplayQuote current;assert(quotes.choose(testMillis,83310,0,guard.price,true,current,live::currentBookMask));
      assert(current.source==4&&current.receivedAt==publishedAt&&current.timestamp==publishedEpoch&&current.price==84567);}
    for(unsigned w=0;w<5;++w)for(const auto& bar:candleSeries[w].bars)if(bar.bucket)assert(bar.high<=8331100);
    schedule(testMillis);assert(market::calls<=calls+1&&needDifferentQuote&&priceReversals==0);
  }
  assert(priceTransitions==300&&priceChanges==1&&multiDigitChanges==1&&selectionHolds==299&&minimumFrameGap==100&&maximumFrameGap==100);
  assert(market::calls==calls+1&&inflight); // One bounded variety request remains in flight while tube deadlines continue.
  market::complete(false);market::queued.deferred=true;processResult(testMillis);calls=market::calls;
  // Invalidating the producer's book state immediately removes its expired
  // publication from selection; neither reconnection nor fake receipt refresh
  // can resurrect it. The last real display may hold only until its original
  // stale deadline; it cannot fall back to the forbidden prior distinct value.
  live::currentBookMask=0;testMillis+=100;incoming(0,83310.4,btc::QuoteKind::LastTrade);
  uint32_t lastAccepted=acceptedQuotes,realReceipt=lastQuoteReceived;
  packets.clear();refreshNixies(testMillis);assertPriceFrame(84567);
  assert(shownSource==4&&shownQuoteReceived==publishedAt&&shownQuoteTimestamp==publishedEpoch&&needDifferentQuote);
  btc::DisplayQuote selected;assert(quotes.choose(testMillis,83310,0,guard.price,true,selected,0)&&selected.source==0);
  testMillis+=100;packets.clear();refreshNixies(testMillis);assert(!shownPrice&&packets.size()==6); // The held receipt is now older than 30 s, despite fresh forbidden A.
  for(const auto& packet:packets)for(unsigned digit=1;digit<16;++digit)assert(packet.bytes[digit]==0x80);
  // Existing stale blanking and provider cooldowns continue to apply when
  // neither the canonical feed nor a valid current book remains available.
  live::healthy=false;testMillis=realReceipt+btc::staleAfterMs(100)+1;packets.clear();refreshNixies(testMillis);
  assert(!shownPrice&&packets.empty()&&acceptedQuotes==lastAccepted); // Already-blank tubes do not need duplicate SPI writes.
  fx.receivedAt=testMillis;streamWarmupUntil=testMillis;nextQuoteAt=testMillis;
  for(auto& deadline:nextHistoryAt)deadline=testMillis+100000;
  for(auto& state:rotation.states)state.availableAt=testMillis+10000;
  schedule(testMillis);assert(market::calls==calls&&!inflight);
  testMillis+=500;rotation.states[0].availableAt=testMillis;schedule(testMillis);
  assert(market::calls==calls+1&&market::outstanding.kind==market::Kind::Quote&&market::outstanding.source==0);
  market::complete();processResult(testMillis);assert(acceptedQuotes==lastAccepted+1&&!live::busy);
  live::healthy=true;live::currentBookMask=0;live::providerMask=3;live::pending={};
  std::cout<<"PASS: two real values hold the current live book at every 100 ms deadline without A-B-A reversal, timestamp renewal, fake counters or chart observations; one bounded variety request, book invalidation, truthful stale blanking and provider backoff remain enforced\n";
}
void rejectedBookRuntimeTests(){
  resetCandleRuntime();refreshDial.stable=0;live::providerMask=(1U<<0)|(1U<<4);live::currentBookMask=1U<<4;
  quotes.clear();shownPrice=0;nextNixieAt=testMillis;uint32_t start=testMillis;time_t initialEpoch=testEpoch;
  auto incoming=[&](uint8_t source,double price,btc::QuoteKind kind){
    live::pending={};live::pending.source=source;live::pending.receivedAt=testMillis;
    live::pending.quote.price=price;live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.quote.kind=kind;
    live::queued=true;processLive(testMillis);
  };
  incoming(0,83310.4,btc::QuoteKind::LastTrade);packets.clear();refreshNixies(testMillis);assertPriceFrame(83310);
  testMillis=start+100;incoming(4,84567,btc::QuoteKind::BboMidpoint);packets.clear();refreshNixies(testMillis);assertPriceFrame(84567);
  testMillis=start+6100;testEpoch=initialEpoch+6;incoming(0,83310.4,btc::QuoteKind::LastTrade);
  btc::CandleSeries canonical[5];std::copy(candleSeries,candleSeries+5,canonical);uint32_t accepted=acceptedQuotes;
  btc::DisplayQuote selected;assert(quotes.choose(testMillis,83310,0,guard.price,true,selected,live::currentBookMask)&&selected.source==4&&selected.receivedAt==start+100);
  incoming(4,150000,btc::QuoteKind::BboMidpoint); // Valid transport data, rejected market outlier.
  assert(acceptedQuotes==accepted&&guard.price==83310.4&&needConfirmation&&!memcmp(canonical,candleSeries,sizeof(canonical)));
  assert(quotes.choose(testMillis,83310,0,guard.price,true,selected,live::currentBookMask)&&selected.source==0);
  packets.clear();refreshNixies(testMillis);assertPriceFrame(84567);assert(shownSource==4&&shownQuoteReceived==start+100&&selectionHolds==1); // Truthful last-known hold; the old head is no longer eligible.
  testMillis=start+6200;incoming(4,84572,btc::QuoteKind::BboMidpoint);
  assert(acceptedQuotes==accepted+1&&!needConfirmation&&!memcmp(canonical,candleSeries,sizeof(canonical)));
  packets.clear();refreshNixies(testMillis);assertPriceFrame(84572);assert(shownSource==4&&shownQuoteReceived==testMillis);
  assert(quotes.choose(testMillis+6000,83310,0,guard.price,true,selected,live::currentBookMask)&&selected.source==4&&selected.receivedAt==testMillis);
  incoming(4,NAN,btc::QuoteKind::BboMidpoint); // Normalization failure also revokes the head.
  assert(acceptedQuotes==accepted+1&&quotes.choose(testMillis,83310,0,guard.price,true,selected,live::currentBookMask)&&selected.source==0);
  testMillis+=100;incoming(4,84573,btc::QuoteKind::BboMidpoint);assert(acceptedQuotes==accepted+2);
  live::pending.quote.timestamp=uint32_t(testEpoch)-config::kQuoteMaxAgeSeconds-1;live::queued=true;processLive(testMillis);
  assert(acceptedQuotes==accepted+2&&quotes.choose(testMillis,83310,0,guard.price,true,selected,live::currentBookMask)&&selected.source==0);
  assert(!memcmp(canonical,candleSeries,sizeof(canonical)));
  live::currentBookMask=0;live::providerMask=3;live::pending={};
  std::cout<<"PASS: rejected, unnormalizable and stale current BBO publications revoke the previous head's live-state eligibility; a later accepted publication restores eligibility with its real receipt time and leaves canonical candles unchanged\n";
}
void varietyHoldoffRuntimeTests(){
  auto tickAndFrame=[&](uint32_t at,double price){
    testMillis=at;btc::Quote tick;tick.price=price;tick.timestamp=uint32_t(testEpoch);
    acceptQuote(tick,0,at,uint32_t(testEpoch),at);
    quotes.clear();quotes.put(price,0,at,uint32_t(testEpoch));
    packets.clear();refreshNixies(at);assertPriceFrame(price);
  };
  auto prepare=[&](unsigned frequency,uint32_t start){
    resetCandleRuntime();testMillis=start;refreshDial.stable=frequency;cadenceTestActive=false;
    guard.acceptedAt=fx.receivedAt=lastQuoteReceived=lastChartTickAt=start;streamWarmupUntil=start;
    nextQuoteAt=start;for(auto& deadline:nextHistoryAt)deadline=start+300000;
    for(auto& state:rotation.states)state.availableAt=start;
    shownPrice=0;nextNixieAt=start;tickAndFrame(start,83300);
    assert(!needDifferentQuote&&!differentQuoteSince);
  };
  uint32_t start=testMillis+10000;prepare(0,start);unsigned calls=market::calls;
  uint32_t miss=start+100;tickAndFrame(miss,83300);assert(needDifferentQuote&&differentQuoteSince==miss);
  schedule(testMillis);assert(market::calls==calls&&!inflight&&!live::busy&&live::connectionCount==2);
  for(unsigned elapsed=100;elapsed<=4900;elapsed+=100){tickAndFrame(miss+elapsed,83300);schedule(testMillis);
    assert(market::calls==calls&&!live::busy&&differentQuoteSince==miss);}
  testMillis=miss+4999;schedule(testMillis);assert(market::calls==calls);
  tickAndFrame(miss+5000,83300);schedule(testMillis);
  assert(market::calls==calls+1&&market::outstanding.kind==market::Kind::Quote&&live::busy);
  market::complete();processResult(testMillis);assert(!live::busy);
  tickAndFrame(miss+5100,83342);assert(!needDifferentQuote&&!differentQuoteSince);schedule(testMillis);assert(market::calls==calls+1);
  tickAndFrame(miss+5200,83342);assert(needDifferentQuote&&differentQuoteSince==testMillis);schedule(testMillis);assert(market::calls==calls+1);

  // Slow settings wait a whole selected period after their first repeated
  // frame, rather than launching variety work five seconds into a minute.
  start=testMillis+10000;prepare(4,start);calls=market::calls;miss=start+60000;
  tickAndFrame(miss,83300);schedule(testMillis);assert(market::calls==calls&&differentQuoteSince==miss);
  testMillis=miss+59999;lastQuoteReceived=testMillis;schedule(testMillis);assert(market::calls==calls&&!live::busy);
  tickAndFrame(miss+60000,83300);schedule(testMillis);assert(market::calls==calls+1&&market::outstanding.kind==market::Kind::Quote);
  market::complete();processResult(testMillis);

  // Lost live reception and an independent confirmation bypass variety's
  // waiting period. Their ordinary provider cooldowns remain respected.
  resetCandleRuntime();for(auto& deadline:nextHistoryAt)deadline=testMillis+100000;
  live::healthy=false;nextQuoteAt=testMillis;calls=market::calls;schedule(testMillis);
  assert(market::calls==calls+1&&market::outstanding.kind==market::Kind::Quote&&!differentQuoteSince);
  market::complete();processResult(testMillis);
  resetCandleRuntime();for(auto& deadline:nextHistoryAt)deadline=testMillis+100000;
  needConfirmation=true;nextQuoteAt=testMillis;calls=market::calls;schedule(testMillis);
  assert(market::calls==calls+1&&market::outstanding.kind==market::Kind::Quote&&!differentQuoteSince);
  market::complete();processResult(testMillis);

  // Unsigned elapsed time must survive wrap, including a first repeated frame
  // exactly at zero. Zero is a valid timestamp, not an inactive timer sentinel.
  for(uint32_t initial:{UINT32_MAX-3000,UINT32_MAX-99}){
    prepare(0,initial);calls=market::calls;miss=initial+100;tickAndFrame(miss,83300);
    assert(needDifferentQuote&&differentQuoteSince==miss);
    for(unsigned elapsed=100;elapsed<=4900;elapsed+=100){tickAndFrame(miss+elapsed,83300);schedule(testMillis);
      assert(market::calls==calls&&differentQuoteSince==miss&&!live::busy);}
    testMillis=miss+4999;schedule(testMillis);assert(market::calls==calls);
    tickAndFrame(miss+5000,83300);schedule(testMillis);assert(market::calls==calls+1&&market::outstanding.kind==market::Kind::Quote);
    market::complete();processResult(testMillis);
  }
  needDifferentQuote=false;differentQuoteSince=0;
  std::cout<<"PASS: repeated 100 ms frame preserves both feeds; variety waits 5 seconds or the slower dial period, resets on real changes, bypasses for outages/confirmation and survives millis wrap including a zero start\n";
}
void startupAndDialRegressionTests(){
  for(auto& s:candleSeries)s={}; // Fresh-boot chart samples.
  testMillis+=6000;refreshDial=btc::Dial(0);guard=btc::PriceGuard{};quotes.clear();priceSequence.clear();shownPrice=0;
  display::blankPrice();packets.clear();refreshNixies(testMillis);
  assert(packets.size()==1&&packets[0].selected==16&&packets[0].bytes[10]==0xC0);
  testMillis+=100;packets.clear();refreshNixies(testMillis);draw(testMillis);
  packets.erase(std::remove_if(packets.begin(),packets.end(),[](const SpiPacket& p){return p.bus==HSPI;}),packets.end());
  assert(packets.size()==2&&packets[0].selected==16&&packets[1].selected==17);
  // The first valid quote must replace the startup chase on this same loop,
  // even if the dial previously had a deadline an hour away and no other
  // exchange has connected yet.
  nextNixieAt=testMillis+3600000;
  auto tick=[&](double price){live::pending.quote.price=price;live::pending.quote.timestamp=uint32_t(testEpoch);live::pending.receivedAt=testMillis;live::pending.source=0;live::queued=true;processLive(testMillis);};
  tick(80000.25);assert(guard.price==80000.25&&!guard.confirmed()&&needConfirmation);
  packets.clear();refreshNixies(testMillis);assertPriceFrame(80000);assert(nextNixieAt==testMillis+100);
  testMillis+=500;tick(80000.25);packets.clear();refreshNixies(testMillis);assertPriceFrame(80000);
  testMillis+=500;tick(80000.25);packets.clear();refreshNixies(testMillis);assertPriceFrame(80000);
  uint32_t received=lastQuoteReceived;uint32_t last=shownDigits;
  for(unsigned frame=0;frame<24;++frame){ // 12-second reconnect, no fabricated input ticks.
    testMillis+=500;packets.clear();refreshNixies(testMillis);assert(shownDigits==last);
    assertPriceFrame(shownDigits);assert(std::abs(double(shownDigits)-80000.25)<=0.5);
    assert(lastQuoteReceived==received&&shownQuoteReceived==received&&guard.price==80000.25);last=shownDigits;
  }
  // Replay the fresh lowest-detent measurements after a stable 1-hour
  // setting. The assembled frequency switch is reversed; all voltages in
  // this independently recorded band must return to 500 ms after debounce.
  refreshDial=btc::Dial(4);refreshDial.initialized=true;nextNixieAt=testMillis+3600000;nextDialsAt=testMillis;
  for(uint32_t measured:{1766U,1774U,1776U,1769U,1775U,1770U,1774U}){
    adc[35]=measured;readDials(testMillis);testMillis+=25;
  }
  assert(refreshDial.initialized&&refreshDial.stable==0&&nextNixieAt==testMillis-25);
  tick(80000.25);packets.clear();refreshNixies(testMillis);assertPriceFrame(80000);
  uint32_t deadline=nextNixieAt;assert(deadline==testMillis+75);
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
  std::cout<<"PASS: first-source display, startup chase, 500 ms tube packets during a 12-second reconnect without new ticks, fresh reversed-contact measurements recovering from 1-hour frequency, dial-independent confirmation and OTA without market data\n";
}
void tubeDiagnosticTests(){
  guard=btc::PriceGuard{};quotes.clear();priceSequence.clear();testMillis+=6000;Serial.input="tube-test\n";serialCommands(testMillis);
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
void temporaryCadenceTests(){
  // A bare ESP32 has no selector attached. Exercise the real price path with
  // an explicit, bounded override while preserving both mechanical settings.
  for(uint32_t start:{testMillis+6000,UINT32_MAX-14999}){
    testMillis=start;refreshDial.stable=4;timelineDial.stable=3;
    guard=btc::PriceGuard{};guard.accept(83300,0,start);guard.accept(83300,1,start);
    quotes.clear();priceSequence.clear();shownPrice=0;lastQuoteReceived=start;
    Serial.input="cadence-test\n";serialCommands(start);
    assert(cadenceTestActive&&refreshPeriod()==500&&refreshDial.stable==4);
    uint32_t frames=nixieFrames;
    for(unsigned tick=0;tick<60;++tick){
      testMillis=start+tick*500;lastQuoteReceived=testMillis;
      packets.clear();refreshNixies(testMillis);assert(packets.size()==6);
      assert(nixieFrames==frames+tick+1&&nextNixieAt==testMillis+500);
      nextTelemetryAt=testMillis;publishTelemetry(testMillis);
      assert(telemetry::last.cadenceTest&&telemetry::last.interval==500&&telemetry::last.frequency.selected==4);
      testMillis+=499;packets.clear();refreshNixies(testMillis);assert(packets.empty());
    }
    testMillis=start+30000;lastQuoteReceived=testMillis;packets.clear();refreshNixies(testMillis);
    assert(!cadenceTestActive&&refreshPeriod()==60000&&packets.size()==6&&nextNixieAt==testMillis+60000);
    assert(refreshDial.stable==4&&timelineDial.stable==3);
  }
  telemetry::queued.push_back(telemetry::Command::Cadence);remoteCommands(testMillis);assert(cadenceTestActive);
  telemetry::queued.push_back(telemetry::Command::Chart);remoteCommands(testMillis);assert(!cadenceTestActive&&refreshPeriod()==60000);
  std::cout<<"PASS: bounded live-price 500 ms diagnostic writes six modules on every deadline, exposes override, retains dials, expires/cancels and handles millis rollover\n";
}
void assembledDialTests(){
  // Independent medians from the fresh range-first / frequency-second sweep.
  const uint16_t frequency[5]={1774,1328,915,490,142},range[5]={1775,1328,909,493,142};
  const uint32_t periods[5]={100,500,1500,10000,60000},windows[5]={30,60,300,1800,3600};
  for(int position=0;position<5;++position){
    testMillis+=6000;nextDialsAt=testMillis;
    refreshDial=btc::Dial((position+4)%5);refreshDial.initialized=true;
    timelineDial=btc::Dial((position+4)%5);timelineDial.initialized=true;
    adc[35]=frequency[position];adc[34]=range[position];nextNixieAt=testMillis+3600000;
    readDials(testMillis);assert(refreshDial.stable!=position&&timelineDial.stable!=position);
    testMillis+=151;readDials(testMillis);
    assert(refreshDial.stable==position&&timelineDial.stable==position&&nextNixieAt==testMillis);
    assert(nextDrawAt==testMillis&&nextHistoryAt[position]==testMillis);
    assert(config::kRefreshMs[refreshDial.stable]==periods[position]&&config::kWindowSeconds[timelineDial.stable]==windows[position]);
    guard.price=83300;quotes.clear();shownPrice=83300;shownDigits=83300;lastQuoteReceived=testMillis;
    packets.clear();refreshNixies(testMillis);assertPriceFrame(83300);
    assert(nextNixieAt==testMillis+periods[position]);
    for(uint16_t invalid:{uint16_t(2269),uint16_t(3300)}){
      adc[35]=adc[34]=invalid;testMillis+=25;readDials(testMillis);testMillis+=151;readDials(testMillis);
      assert(refreshDial.stable==position&&timelineDial.stable==position);
    }
  }
  std::cout<<"PASS: fresh assembled switch measurements select all five intended frequency/range settings, debounce, immediate real SPI output and unused/open-contact rejection\n";
}
void shortRangeAndMotionTests(){
  testMillis+=6000;testEpoch=1800000600;for(auto& s:candleSeries)s={};
  for(unsigned w=0;w<5;++w){candleSeries[w]={};candleScales[w]={};candleDirty[w]=false;nextCandleSave[w]=0;}
  guard=btc::PriceGuard{};quotes.clear();refreshDial.stable=0;timelineDial.stable=0;shownPrice=0;needConfirmation=false;
  uint32_t start=testMillis;unsigned writes=fsMock().writes;
  for(unsigned second=0;second<35;++second){
    testMillis=start+second*1000;testEpoch=1800000600+second;btc::Quote q;q.timestamp=uint32_t(testEpoch);q.price=81000+second;
    acceptQuote(q,0,testMillis,uint32_t(testEpoch),testMillis);saveHistory(testMillis);if(market::sent){market::complete();processResult(testMillis);}
    assert(candleSeries[0].count(0,chartNowMs())<=21&&candleSeries[1].count(1,chartNowMs())<=21);
    if(second<30)assert(fsMock().writes==writes);
  }
  assert(candleSeries[0].count(0,chartNowMs())==21&&fsMock().writes>writes);
  auto plot=btc::makeCandles(candleSeries[0],candleScales[0],0,chartNowMs());assert(__builtin_popcount(plot.valid)==21);
  // Ten actual deadlines per second retain different real whole-dollar values.
  quotes.clear();priceSequence.clear();quotes.put(81199,0,testMillis,uint32_t(testEpoch));quotes.put(81203,1,testMillis,uint32_t(testEpoch));quotes.put(81241,2,testMillis,uint32_t(testEpoch));quotes.put(81352,3,testMillis,uint32_t(testEpoch));
  guard.price=81203;lastQuoteReceived=testMillis;shownPrice=0;nextNixieAt=testMillis;
  for(unsigned frame=0;frame<10;++frame){testMillis+=100;packets.clear();refreshNixies(testMillis);assert(packets.size()==6);}
  assert(priceTransitions==9&&priceChanges==9&&multiDigitChanges==9&&minimumFrameGap==100&&maximumFrameGap==100);
  std::cout<<"PASS: 100 ms distinct real multi-digit frames, 21 fixed OHLC columns from live quotes, bounded range caches and flash checkpoints instead of per-tick writes\n";
}
void asyncCacheTests(){
  testMillis+=6000;testEpoch=1800001200;guard=btc::PriceGuard{};guard.accept(83300,0,testMillis);guard.accept(83300,1,testMillis);
  refreshDial.stable=0;shownPrice=0;lastQuoteReceived=testMillis;quotes.clear();live::busy=false;
  for(unsigned w=0;w<5;++w){candleDirty[w]=false;nextCandleSave[w]=0;}
  candleSeries[2]={};candleSeries[2].add(chartNowMs(),83300,2,chartNowMs());candleScales[2].update(candleSeries[2],2,chartNowMs());
  auto snapshot=candleSeries[2];markCandleDirty(2,testMillis);nextCandleSave[2]=testMillis;unsigned writes=fsMock().writes;
  uint32_t warmup=streamWarmupUntil;saveHistory(testMillis);assert(inflight&&market::sent&&market::outstanding.kind==market::Kind::SaveCache&&!live::busy&&fsMock().writes==writes);
  btc::Quote tick;tick.price=83320;tick.timestamp=uint32_t(testEpoch);acceptQuote(tick,0,testMillis,uint32_t(testEpoch),testMillis);
  assert(candleDirty[2]);packets.clear();refreshNixies(testMillis);assertPriceFrame(83320);testMillis+=100;packets.clear();refreshNixies(testMillis);assert(packets.size()==6&&fsMock().writes==writes);
  market::complete();processResult(testMillis);assert(!inflight&&candleDirty[2]&&!live::busy&&streamWarmupUntil==warmup);
  assert(candleSavedTime[2]/1000==uint64_t(testEpoch));btc::CandleSeries restored[5];btc::CandleScale axes[5];CandleStore boot;assert(boot.begin(restored,axes));assert(!memcmp(&snapshot,&restored[2],sizeof(snapshot)));
  unsigned calls=market::calls;saveHistory(testMillis);assert(market::calls==calls); // New ticks wait for the next 30 s checkpoint.
  std::cout<<"PASS: immutable asynchronous checkpoints leave display deadlines/streams responsive and preserve live changes for the next save\n";
}
void streamStarvationRegressionTests(){
  liveReady=true;live::healthy=false;testMillis+=6000;refreshDial.stable=timelineDial.stable=0;
  guard=btc::PriceGuard{};guard.accept(84191,0,testMillis);guard.accept(84191,1,testMillis);
  shownPrice=84191;shownDigits=84191;lastQuoteReceived=testMillis;needConfirmation=false;needDifferentQuote=true;differentQuoteSince=testMillis-5000;
  market::outstanding={market::Kind::Quote,0,0,uint32_t(testEpoch)};market::sent=true;inflight=true;
  market::complete();processResult(testMillis);uint32_t resume=testMillis;
  assert(streamWarmupUntil==resume+35000&&!live::busy);
  unsigned calls=market::calls;
  for(unsigned elapsed=0;elapsed<35000;elapsed+=500){
    testMillis=resume+elapsed;nextQuoteAt=testMillis;nextHistoryAt[0]=testMillis;
    schedule(testMillis);assert(market::calls==calls&&!inflight&&!live::busy);
  }
  // A local allocation deferral preserves provider cooldowns and the stream;
  // repeated quotes ask another source without waiting on the display core.
  live::healthy=true;testMillis=resume+35000;
  for(auto& state:rotation.states){state.availableAt=testMillis;state.failures=0;}
  for(auto& deadline:nextHistoryAt)deadline=testMillis+100000;
  nextQuoteAt=testMillis;schedule(testMillis);assert(market::sent&&market::outstanding.kind==market::Kind::Quote);
  uint8_t source=market::outstanding.source;uint32_t cooldown=rotation.states[source].availableAt;
  market::complete(false);market::queued.deferred=true;processResult(testMillis);
  assert(!rotation.states[source].failures&&rotation.states[source].availableAt==cooldown&&!live::busy);
  nextQuoteAt=testMillis+100000;needDifferentQuote=false;
  std::cout<<"PASS: 500 ms/5 min regression gives streams 35 seconds to reconnect, prevents continuous HTTP starvation, distinguishes local RAM deferral and requests different real quotes independently of display timing\n";
}
void wirelessRuntimeTests(){
  live::healthy=true;lastQuoteReceived=testMillis+6000;
  testMillis+=6000;nextDialsAt=nextRawAt=nextTelemetryAt=testMillis;adc[35]=1774;adc[34]=909;readDials(testMillis);
  assert(frequencyMv==1774&&rangeMv==909&&frequencyDecoded==0&&rangeDecoded==2);
  publishTelemetry(testMillis);assert(telemetry::last.frequency.mv==1774&&telemetry::last.frequency.decoded==0&&telemetry::last.range.mv==909);
  assert(telemetry::last.frames==nixieFrames&&telemetry::last.accepted==acceptedQuotes&&telemetry::last.bootConfirmed);
  // A queued OTA must wait for ongoing HTTP, but bypass six-hour/stream
  // grace once it is free. It always dispatches the ordinary signed updater.
  telemetry::queued.push_back(telemetry::Command::Ota);remoteCommands(testMillis);assert(forceOtaPending);
  unsigned calls=market::calls;inflight=true;schedule(testMillis);assert(forceOtaPending&&market::calls==calls);inflight=false;
  streamWarmupStarted=true;streamWarmupUntil=testMillis+35000;ota::validBoot=false;schedule(testMillis);
  assert(forceOtaPending&&market::calls==calls);ota::validBoot=true;schedule(testMillis);
  assert(!forceOtaPending&&market::sent&&market::outstanding.kind==market::Kind::Ota);market::complete();processResult(testMillis);
  telemetry::queued.push_back(telemetry::Command::Readings);remoteCommands(testMillis);assert(matrixDiagnostics&&diagnosticPage==1);
  packets.clear();draw(testMillis);assert(!packets.empty());for(const auto&p:packets)assert(p.bus==HSPI);
  telemetry::queued.push_back(telemetry::Command::Tubes);remoteCommands(testMillis);assert(tubeTestEndsAt==testMillis+5000);
  telemetry::queued.push_back(telemetry::Command::Chart);remoteCommands(testMillis);assert(!matrixDiagnostics&&!tubeTestEndsAt);
  // BOOT release after one second toggles diagnostics; taps cycle pages.
  pins[0]=LOW;serialCommands(testMillis);testMillis+=1000;pins[0]=HIGH;serialCommands(testMillis);assert(matrixDiagnostics&&diagnosticPage==0);
  pins[0]=LOW;serialCommands(testMillis);testMillis+=60;pins[0]=HIGH;serialCommands(testMillis);assert(diagnosticPage==1);
  pins[0]=LOW;serialCommands(testMillis);testMillis+=1000;pins[0]=HIGH;serialCommands(testMillis);assert(!matrixDiagnostics);
  matrixDiagnostics=true;pins[0]=LOW;serialCommands(testMillis);testMillis+=5000;serialCommands(testMillis);
  assert(bootHandled&&!matrixDiagnostics&&wifi.portalActive());pins[0]=HIGH;serialCommands(testMillis);assert(!matrixDiagnostics);
  WiFi.connection=WL_CONNECTED;wifi.loop(testMillis);testMillis+=8001;wifi.loop(testMillis);assert(wifi.online()&&!wifi.portalActive());
  std::cout<<"PASS: actual dial sampling/snapshots, queued signed OTA checks bypassing schedule without interrupting in-flight HTTP, wireless matrix/tube controls and BOOT diagnostics gestures\n";
}
int main(){settingsTests();cacheRuntimeTests();sharedCandleRuntimeTests();liveFxRuntimeTests();secondStreamWarmupRuntimeTests();nixieCadenceTests();quoteSelectionRuntimeTests();sequencedPriceRuntimeTests();convertedBookRuntimeTests();canonicalChartRuntimeTests();quietCurrentBookRuntimeTests();rejectedBookRuntimeTests();varietyHoldoffRuntimeTests();startupAndDialRegressionTests();tubeDiagnosticTests();temporaryCadenceTests();assembledDialTests();shortRangeAndMotionTests();asyncCacheTests();streamStarvationRegressionTests();wirelessRuntimeTests();soak();}
