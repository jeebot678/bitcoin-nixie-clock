#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <time.h>
#include "ClockCore.h"
#include "Display.h"
#include "MarketClient.h"
#include "Provisioning.h"
#include "OtaUpdate.h"
#include "OtaConfig.h"
#include "CandleStore.h"
#include <sys/time.h>
#include "LivePrice.h"
#include "QuoteBook.h"
#include "DisplayMotion.h"
#include "DeviceTelemetry.h"
#include "NetworkMemory.h"
#ifdef BTC_SKIT_DEMO
#include "SkitChart.h"
#endif

namespace {
Provisioning wifi;
// Default to the fastest setting until the selector is confirmed.
btc::Dial refreshDial(0),timelineDial;
btc::Rotation rotation;
btc::FxRate fx;
btc::PriceGuard guard;
btc::QuoteBook quotes;
btc::QuoteSequence priceSequence;
btc::CandleSeries candleSeries[5];
btc::CandleScale candleScales[5];
CandleStore candleStore;
bool candleDirty[5]={},candleBackfilled[5]={};
uint32_t nextCandleSave[5]={};
uint64_t candleSavedTime[5]={};
uint32_t candleReferencePrice[5]={};
uint64_t chartContinuousSince=0;
uint32_t lastChartTickAt=0;
bool networkReady=false,inflight=false,timeStarted=false;
bool liveReady=false,streamWarmupStarted=false;
uint32_t streamWarmupUntil=0,firstQuoteFallbackAt=0;
uint32_t initialOtaGraceUntil=0;
bool showingSetup=false;
bool needConfirmation=true,logNixies=false;
bool needDifferentQuote=false;
uint32_t differentQuoteSince=0;
uint8_t httpKind=255,httpSource=255;
int lastHttpStatus=0;
uint32_t chartColumns=0;
uint16_t chartPixels=0;
double shownPrice=0;
uint8_t shownSource=0,lastAcceptedSource=0;
btc::QuoteKind shownQuoteKind=btc::QuoteKind::LastTrade,lastAcceptedKind=btc::QuoteKind::LastTrade;
uint8_t chartSource=255;
uint32_t chartQuoteReceived=0;
uint32_t shownQuoteReceived=0;
uint32_t shownQuoteTimestamp=0;
uint32_t shownDigits=0;
uint32_t nextNixieAt=0,nixieFrames=0,lastNixieAt=0,acceptedQuotes=0;
uint32_t lastFrameGap=0,lastFramePeriod=0,minimumFrameGap=0,maximumFrameGap=0;
uint32_t priceTransitions=0,priceChanges=0,multiDigitChanges=0;
uint32_t priceReversals=0,selectionHolds=0;
bool displayEstimated=false;
uint32_t estimatedFrames=0;
uint32_t nextTelemetryAt=0,nextRawAt=0;
uint16_t frequencyMv=0,rangeMv=0,frequencyRaw=0,rangeRaw=0;
int frequencyDecoded=-1,rangeDecoded=-1;
bool forceOtaPending=false,matrixDiagnostics=false;
uint8_t diagnosticPage=0;
uint32_t nextQuoteAt=0,nextDialsAt=0,nextDrawAt=0,lastPriceEpoch=0,lastQuoteReceived=0;
uint32_t nextHistoryAt[5]={},historyErrors[5]={};
uint8_t historySource[5]={2,2,2,2,2}; // Bounded Bitstamp pages preserve primary TLS.
uint32_t bootPressedAt=0,testEndsAt=0,tubeTestEndsAt=0;
uint32_t cadenceTestEndsAt=0;
bool cadenceTestActive=false;
bool bootHandled=false;
String serialLine;

uint64_t chartNowMs(){
#ifdef ARDUINO
  timeval value;gettimeofday(&value,nullptr);return uint64_t(value.tv_sec)*1000+unsigned(value.tv_usec)/1000;
#else
  return uint64_t(time(nullptr))*1000;
#endif
}
bool isHistory(market::Kind kind){return kind==market::Kind::History||kind==market::Kind::Candles;}
void markCandleDirty(unsigned window,uint32_t now){candleDirty[window]=true;if(!nextCandleSave[window])nextCandleSave[window]=now+config::kShortHistorySaveMs;}
uint32_t refreshPeriod(){
#ifdef BTC_SKIT_DEMO
  return skit::kRefreshMs;
#else
  return cadenceTestActive?500:config::kRefreshMs[refreshDial.stable];
#endif
}
void startCadenceTest(uint32_t now){cadenceTestActive=true;cadenceTestEndsAt=now+30000;nextNixieAt=now;}

uint32_t millivolts(uint8_t pin) {
  uint32_t readings[7];
  for (uint8_t i=0;i<7;++i) readings[i]=analogReadMilliVolts(pin);
  std::sort(readings,readings+7); return readings[3];
}
void readDials(uint32_t now) {
  if (!btc::due(now,nextDialsAt)) return;
  nextDialsAt=now+25;
  frequencyMv=uint16_t(millivolts(config::kRefreshPin));rangeMv=uint16_t(millivolts(config::kTimelinePin));
  frequencyDecoded=btc::dialPosition(frequencyMv,config::kRefreshMv);
  rangeDecoded=btc::dialPosition(rangeMv,config::kTimelineMv);
  if(btc::due(now,nextRawAt)){
    nextRawAt=now+250;
    uint16_t samples[7];
    for(unsigned i=0;i<7;++i)samples[i]=analogRead(config::kRefreshPin);
    std::sort(samples,samples+7);frequencyRaw=samples[3];
    for(unsigned i=0;i<7;++i)samples[i]=analogRead(config::kTimelinePin);
    std::sort(samples,samples+7);rangeRaw=samples[3];
  }
  const int previousFrequency=refreshDial.stable;
  if (refreshDial.update(frequencyDecoded,now)) {
    if (previousFrequency!=refreshDial.stable) {nextQuoteAt=now;nextNixieAt=now;}
    Serial.printf("Frequency JSEL2/GPIO35 position %d: %lu ms\n",refreshDial.stable+1,(unsigned long)config::kRefreshMs[refreshDial.stable]);
  }
  if (timelineDial.update(rangeDecoded,now)) {
    nextHistoryAt[timelineDial.stable]=now; nextDrawAt=now;
    Serial.printf("Range JSEL1/GPIO34 position %d: %s (%u cached candles)\n",timelineDial.stable+1,config::kWindowNames[timelineDial.stable],candleSeries[timelineDial.stable].count(timelineDial.stable,chartNowMs()));
  }
}
void remoteCommands(uint32_t now) {
  telemetry::Command command;
  for(unsigned i=0;i<4&&telemetry::receive(command);++i){
    switch(command){
      case telemetry::Command::Ota:forceOtaPending=true;break;
      case telemetry::Command::Tubes:tubeTestEndsAt=now+5000;break;
      case telemetry::Command::Cadence:startCadenceTest(now);break;
      case telemetry::Command::Readings:matrixDiagnostics=true;diagnosticPage=1;nextDrawAt=now;break;
      case telemetry::Command::Chart:matrixDiagnostics=false;tubeTestEndsAt=0;cadenceTestActive=false;nextNixieAt=now;nextDrawAt=now;break;
    }
  }
}
void publishTelemetry(uint32_t now) {
  if(!btc::due(now,nextTelemetryAt))return;
  nextTelemetryAt=now+250;
  telemetry::Snapshot s;s.uptime=now;s.heap=ESP.getFreeHeap();s.largest=ESP.getMaxAllocHeap();
#ifdef ARDUINO
  s.resetReason=uint8_t(esp_reset_reason());
#endif
  s.dataHeap=network_memory::freeBytes();s.dataBlock=network_memory::largestBlock();s.httpStack=market::stackRemaining();
  s.httpKind=inflight?httpKind:255;s.httpSource=httpSource;s.httpStatus=lastHttpStatus;s.quoteRequested=needDifferentQuote;
  s.chartColumns=chartColumns;s.chartPixels=chartPixels;s.historyCandles=candleSeries[timelineDial.stable].count(timelineDial.stable,chartNowMs());s.historySavedAt=uint32_t(candleSavedTime[timelineDial.stable]/1000);
  s.axisLow=candleScales[timelineDial.stable].low;s.axisStep=candleScales[timelineDial.stable].step;
  s.online=wifi.online();s.portal=wifi.portalActive();s.clock=time(nullptr)>=config::kMinimumEpoch;
  s.frequency.mv=frequencyMv;s.frequency.raw=frequencyRaw;s.frequency.decoded=frequencyDecoded;
  s.frequency.selected=refreshDial.stable;s.frequency.initialized=refreshDial.initialized;
  s.range.mv=rangeMv;s.range.raw=rangeRaw;s.range.decoded=rangeDecoded;
  s.range.selected=timelineDial.stable;s.range.initialized=timelineDial.initialized;
  s.interval=refreshPeriod();s.cadenceTest=cadenceTestActive;s.window=config::kWindowSeconds[timelineDial.stable];
  auto liveState=live::status();s.connections=liveState.connections;s.quotes=liveState.quotes;
  s.streamProviders=liveState.providers;s.streamDropped=liveState.dropped;s.streamRejected=liveState.rejected;s.streamDisconnects=liveState.disconnects;
  s.currentBooks=liveState.currentBooks;
  s.currentConvertedBooks=liveState.currentConvertedBooks;
  s.streamStack=liveState.stackFreeBytes;
  for(unsigned source=0;source<btc::kSourceCount;++source){s.sourceQuotes[source]=liveState.quoteCounts[source];s.sourceAges[source]=liveState.quoteAges[source];s.sourceFailures[source]=liveState.failures[source];}
  for(unsigned i=0;i<2;++i)s.streamSlots[i]=liveState.slots[i];
  s.accepted=acceptedQuotes;s.frames=nixieFrames;s.quoteAge=now-lastQuoteReceived;s.frameAge=now-lastNixieAt;
  s.frameGap=lastFrameGap;s.nextFrame=uint32_t(std::max<int32_t>(0,int32_t(nextNixieAt-now)));
  s.minimumGap=minimumFrameGap;s.maximumGap=maximumFrameGap;
  s.transitions=priceTransitions;s.priceChanges=priceChanges;s.multiChanges=multiDigitChanges;
  s.priceReversals=priceReversals;s.selectionHolds=selectionHolds;
  s.displayEstimated=displayEstimated;s.estimatedFrames=estimatedFrames;
  s.marketPrice=guard.price;s.displayPrice=shownPrice;s.digits=shownDigits;s.source=shownSource;
  s.quoteKind=shownQuoteKind;s.displayQuoteAge=shownPrice?now-shownQuoteReceived:UINT32_MAX;
  s.chartSource=chartSource;s.chartQuoteAge=chartSource<btc::kSourceCount?now-chartQuoteReceived:UINT32_MAX;
  bool testing=tubeTestEndsAt&&!btc::due(now,tubeTestEndsAt);
  s.displayMode=testing?3:!guard.price?1:!shownPrice?2:displayEstimated?4:0;
  if(testing)s.testStep=uint8_t((5000-uint32_t(tubeTestEndsAt-now))/500);
  s.matrixPage=matrixDiagnostics?diagnosticPage:255;
  s.confirmed=guard.confirmed();s.inflight=inflight;s.otaPending=forceOtaPending;s.bootConfirmed=ota::bootConfirmed();s.ota=ota::status();telemetry::publish(s);
}
void diagnostics(uint32_t now) {
  Serial.printf("Firmware=%s Wi-Fi=%s portal=%s setup=%s clock=%s heap=%u largest=%u refresh=%lu window=%s price=%.2f stale=%s fx=%.6f fxFresh=%s history=%u\n",
    ota_config::kVersion,wifi.online()?"connected":"offline",wifi.portalActive()?"on":"off",btc::setupStateName(wifi.setupState()),time(nullptr)>=config::kMinimumEpoch?"synced":"waiting",
    ESP.getFreeHeap(),ESP.getMaxAllocHeap(),(unsigned long)config::kRefreshMs[refreshDial.stable],config::kWindowNames[timelineDial.stable],guard.price,
    !guard.price||uint32_t(now-lastQuoteReceived)>btc::staleAfterMs(config::kRefreshMs[refreshDial.stable])?"yes":"no",fx.usd,fx.fresh(now)?"yes":"no",candleSeries[timelineDial.stable].count(timelineDial.stable,chartNowMs()));
  Serial.printf("  History storage=%s savedEpoch=%lu dirty=%s\n",candleStore.ready()?"ready":"unavailable",(unsigned long)uint32_t(candleSavedTime[timelineDial.stable]/1000),candleDirty[timelineDial.stable]?"yes":"no");
  for (uint8_t window=0;window<5;++window) Serial.printf("  Cache %s: %u candles, savedEpoch=%lu\n",config::kWindowNames[window],candleSeries[window].count(window,chartNowMs()),(unsigned long)uint32_t(candleSavedTime[window]/1000));
  for (size_t i=0;i<btc::kSourceCount;++i)
    Serial.printf("  %s failures=%u cooldown=%ld ms\n",btc::kSources[i].name,rotation.states[i].failures,(long)std::max<int32_t>(0,int32_t(rotation.states[i].availableAt-now)));
  auto stream=live::status();
  Serial.printf("  Nixie frames=%lu shown=%.2f latestQuoteAge=%lu ms nextFrame=%ld ms; frequencyDial=%s position=%d; stream=%s quotes=%lu accepted=%lu streamStackMin=%lu B\n",
    (unsigned long)nixieFrames,shownPrice,(unsigned long)uint32_t(now-lastQuoteReceived),(long)std::max<int32_t>(0,int32_t(nextNixieAt-now)),
    refreshDial.initialized?"stable":"unconfirmed",refreshDial.stable+1,stream.connected?"connected":"offline",(unsigned long)stream.quotes,(unsigned long)acceptedQuotes,(unsigned long)stream.stackFreeBytes);
  Serial.printf("  Live connections=%u; displayed source=%s quoteAge=%lu ms digits=%06lu confirmed=%s\n",stream.connections,btc::kSources[shownSource].name,(unsigned long)uint32_t(now-shownQuoteReceived),(unsigned long)shownDigits,guard.confirmed()?"yes":"no");
}
void serialCommands(uint32_t now) {
  while (Serial.available()) {
    char c=char(Serial.read());
    if (c=='\r') continue;
    if (c!='\n') { if (serialLine.length()<64) serialLine+=c; continue; }
    serialLine.trim();
    if (serialLine=="status") diagnostics(now);
    else if (serialLine=="dials") Serial.printf("Frequency JSEL2 GPIO35=%lu mV; range JSEL1 GPIO34=%lu mV\n",(unsigned long)millivolts(config::kRefreshPin),(unsigned long)millivolts(config::kTimelinePin));
    else if (serialLine=="setup") wifi.openSetup();
    else if (serialLine=="wifi-reset") wifi.forget();
    else if (serialLine=="selftest") testEndsAt=now+4500;
    else if (serialLine=="ota-check") forceOtaPending=true;
    else if (serialLine=="cadence-test") {startCadenceTest(now);Serial.println("Live-price cadence test: 500 ms for 30 seconds, then the frequency dial resumes.");}
    else if (serialLine=="readings") {matrixDiagnostics=!matrixDiagnostics;diagnosticPage=1;nextDrawAt=now;}
    else if (serialLine=="tube-test") {
      tubeTestEndsAt=now+5000;
      Serial.println("Tube test: all six digits advance every 500 ms for 5 seconds, then live display resumes.");
    }
    else if (serialLine=="reboot") ESP.restart();
    else if (serialLine=="ticks" || serialLine=="ticks on" || serialLine=="ticks off") {
      logNixies=serialLine=="ticks"?!logNixies:serialLine=="ticks on";
      Serial.println(logNixies?"Nixie frame logging on":"Nixie frame logging off");
    }
    else if (!serialLine.isEmpty()) Serial.println("Commands: status, dials, ticks, tube-test, cadence-test, readings, ota-check, setup, wifi-reset, selftest, reboot");
    serialLine="";
  }
  if (digitalRead(0)==LOW) {
    if (!bootPressedAt) bootPressedAt=now;
    if (!bootHandled && uint32_t(now-bootPressedAt)>=5000) { bootHandled=true;matrixDiagnostics=false;wifi.openSetup(); }
  } else {
    if(bootPressedAt&&!bootHandled){
      uint32_t held=now-bootPressedAt;
      if(held>=1000){matrixDiagnostics=!matrixDiagnostics;diagnosticPage=0;nextDrawAt=now;}
      else if(held>=40&&matrixDiagnostics){diagnosticPage=(diagnosticPage+1)%5;nextDrawAt=now;if(diagnosticPage==4)tubeTestEndsAt=now+5000;}
    }
    bootPressedAt=0;bootHandled=false;
  }
}
void acceptQuote(const btc::Quote& quote,uint8_t source,uint32_t receivedAt,uint32_t epoch,uint32_t now) {
  if (source>=btc::kSourceCount) return;
  double price;
  if (!fx.normalize(quote.price,btc::kSources[source].usdt,now,price)) {
    if(quote.kind==btc::QuoteKind::BboMidpoint||quote.kind==btc::QuoteKind::ConvertedBboMidpoint)quotes.invalidateCurrentBook(source,quote.kind);
    nextQuoteAt=now+500;return;
  }
  if (guard.accept(price,source,now)) {
    lastPriceEpoch=quote.timestamp?quote.timestamp:epoch;
    lastQuoteReceived=receivedAt;++acceptedQuotes;needConfirmation=!guard.confirmed();
    if (needConfirmation) nextQuoteAt=now; // Confirmation never waits for a slow display dial.
    lastAcceptedSource=source;lastAcceptedKind=quote.kind;
    quotes.put(price,source,receivedAt,lastPriceEpoch,quote.kind);
    if(quote.kind==btc::QuoteKind::ConvertedBboMidpoint)return; // Additional USD display quote, never a canonical chart observation.
    // Keep one venue's chart observations consistent. The tubes may select
    // other genuine quotes, including book midpoints with a venue premium.
    uint16_t providers=liveReady?live::status().providers:0;
    unsigned rank=source==0?0:source==1?1:2;
    unsigned currentRank=chartSource==0?0:chartSource==1?1:2;
    uint32_t chartAge=now-chartQuoteReceived;
    bool chartAlive=chartSource<btc::kSourceCount&&(chartAge<=5000||
      (chartAge<=30000&&((providers&(1U<<chartSource))||quote.kind==btc::QuoteKind::BboMidpoint)));
    if(source!=chartSource&&chartAlive&&rank>=currentRank)return;
    bool sameConnected=source==chartSource&&(providers&(1U<<source));
    chartSource=source;chartQuoteReceived=receivedAt;
    uint64_t stampMs=quote.timestampMs?quote.timestampMs:uint64_t(lastPriceEpoch)*1000;
    uint64_t utcMs=chartNowMs();
    uint32_t gapLimit=sameConnected?30000:5000;
    if(!chartContinuousSince||uint32_t(now-lastChartTickAt)>gapLimit){chartContinuousSince=std::min(stampMs,utcMs);for(auto& filled:candleBackfilled)filled=false;}
    lastChartTickAt=now;
    for(unsigned w=0;w<5;++w){
      if(candleSeries[w].add(stampMs,price,w,utcMs))markCandleDirty(w,now);
      uint32_t latest=candleSeries[w].latestCloseCents(w);
      if(latest)candleReferencePrice[w]=latest;
      else if(price<=UINT32_MAX/100.0)candleReferencePrice[w]=uint32_t(std::llround(price*100));
    }
    nextDrawAt=now;
  } else {
    if(quote.kind==btc::QuoteKind::BboMidpoint||quote.kind==btc::QuoteKind::ConvertedBboMidpoint)quotes.invalidateCurrentBook(source,quote.kind);
    needConfirmation=true;nextQuoteAt=now;
  }
}
void processLive(uint32_t now) {
  live::Tick tick;uint32_t started=millis();
  for(unsigned i=0;i<32&&live::receive(tick);++i){
    now=millis();
    bool valid=tick.source<btc::kSourceCount&&wifi.online()&&uint32_t(now-tick.receivedAt)<=5000&&btc::freshTimestamp(tick.quote.timestamp,uint32_t(time(nullptr)));
    if(valid){
      if(tick.source==1&&std::isfinite(tick.fx)&&tick.fx>=0.5&&tick.fx<=1.5&&!tick.quote.price){fx.usd=tick.fx;fx.receivedAt=tick.receivedAt;}
      else if(!tick.fx)acceptQuote(tick.quote,tick.source,tick.receivedAt,tick.quote.timestamp,now);
    }
    else if(tick.quote.kind==btc::QuoteKind::BboMidpoint||tick.quote.kind==btc::QuoteKind::ConvertedBboMidpoint)quotes.invalidateCurrentBook(tick.source,tick.quote.kind);
    if(i>=1&&uint32_t(millis()-started)>=4)break; // Drain bursts without delaying the tube timer.
  }
  live::allowUsdt(fx.fresh(millis()));
}
void refreshNixies(uint32_t now) {
  if(cadenceTestActive&&btc::due(now,cadenceTestEndsAt)){cadenceTestActive=false;nextNixieAt=now;}
  if (tubeTestEndsAt && !btc::due(now,tubeTestEndsAt)) {
    display::testDigits(uint8_t((5000-uint32_t(tubeTestEndsAt-now))/500));
    shownPrice=0;nextNixieAt=now;return;
  }
  tubeTestEndsAt=0;
  if (!guard.price) {display::setupZero(now);shownPrice=0;return;}
  const auto liveState=liveReady?live::status():live::Status{};
  const uint16_t currentBooks=liveState.currentBooks,currentConvertedBooks=liveState.currentConvertedBooks;
  const bool stale=uint32_t(now-lastQuoteReceived)>btc::staleAfterMs(refreshPeriod());
  if (!shownPrice || btc::due(now,nextNixieAt)) {
    const bool firstFrame=!shownPrice;
    const uint32_t previousDigits=shownDigits;
    const uint32_t period=refreshPeriod();
    btc::DisplayQuote selected{guard.price,lastQuoteReceived,lastPriceEpoch,lastAcceptedSource,lastAcceptedKind};
    const uint32_t entropy=esp_random();
    bool selectedQuote=wifi.online()&&quotes.choose(now,firstFrame?0:shownDigits,shownSource,guard.price,fx.fresh(now),selected,currentBooks,&priceSequence,entropy,currentConvertedBooks);
    uint32_t digits=uint32_t(std::floor(selected.price+0.5));
    bool previousNeedDifferent=needDifferentQuote;
    // Motion must not make an unchanged real market quote look like new data
    // to the fetch scheduler. Its diversity/backoff policy remains independent.
    needDifferentQuote=!firstFrame&&(!selectedQuote||digits==uint32_t(std::floor(shownPrice+0.5))||priceSequence.reverses(digits));
    if(!needDifferentQuote)differentQuoteSince=0;
    else if(!previousNeedDifferent)differentQuoteSince=now;
    const bool genuine=selectedQuote||(wifi.online()&&!stale&&
      (!btc::kSources[lastAcceptedSource].usdt||fx.fresh(now)));
    displayEstimated=!genuine||(!firstFrame&&digits==previousDigits)||priceSequence.reverses(digits);
    if(displayEstimated){digits=btc::DisplayMotion::choose(selected.price,previousDigits,priceSequence,entropy);++estimatedFrames;}
    shownDigits=digits;
    bool reversal=priceSequence.reverses(digits);
    priceSequence.record(digits);
    shownQuoteKind=selected.kind;
    display::price(double(shownDigits),true);shownPrice=selected.price;shownSource=selected.source;shownQuoteReceived=selected.receivedAt;shownQuoteTimestamp=selected.timestamp;++nixieFrames;
    lastFrameGap=now-lastNixieAt;
    if(lastFramePeriod!=period||firstFrame){minimumFrameGap=maximumFrameGap=0;lastFramePeriod=period;priceTransitions=priceChanges=multiDigitChanges=priceReversals=selectionHolds=0;}
    else {++priceTransitions;priceChanges+=shownDigits!=previousDigits;multiDigitChanges+=btc::QuoteBook::changedDigits(previousDigits,shownDigits)>=2;priceReversals+=reversal;selectionHolds+=shownDigits==previousDigits;if(!minimumFrameGap||lastFrameGap<minimumFrameGap)minimumFrameGap=lastFrameGap;if(lastFrameGap>maximumFrameGap)maximumFrameGap=lastFrameGap;}
    if (logNixies || nixieFrames==1) Serial.printf("Nixie frame at %lu ms: %.2f interval=%lu ms gap=%lu ms quoteAge=%lu ms source=%s digits=%lu estimated=%s\n",(unsigned long)now,shownPrice,
      (unsigned long)period,(unsigned long)uint32_t(now-lastNixieAt),(unsigned long)uint32_t(now-shownQuoteReceived),btc::kSources[shownSource].name,(unsigned long)shownDigits,displayEstimated?"yes":"no");
    lastNixieAt=now;
    // Preserve the timer phase through normal loop jitter. If work delays a
    // frame, send the latest value once and skip expired deadlines.
    nextNixieAt=firstFrame?now+period:nextNixieAt+(uint32_t(now-nextNixieAt)/period+1)*period;
  }
}
void processResult(uint32_t now) {
  market::ResultLease lease;
  if (!market::receive(lease)) return;
  const market::Result& result=lease.get();
  now=millis();
  inflight=false;
  if(result.request.kind==market::Kind::SaveCache){
    unsigned w=result.request.window;
    if(result.ok)candleSavedTime[w]=result.cacheEpochMs;
    else{candleDirty[w]=true;nextCandleSave[w]=now+config::kHistorySaveRetryMs;}
    return; // Flash jobs neither pause streams nor change provider cooldowns.
  }
  lastHttpStatus=result.status;
  live::httpBusy(false);
  // Every HTTP response must give missing streams time to reconnect. Without
  // this, 500 ms REST/FX/history dispatches keep busy=true indefinitely.
  if(liveReady&&(!live::fresh(now)||live::status().connections<2))streamWarmupUntil=now+35000;
  if (result.request.kind==market::Kind::Ota) { nextQuoteAt=now;return; }
  size_t source=result.request.source;
  if(result.deferred){
    if(isHistory(result.request.kind)){uint8_t window=result.request.window;nextHistoryAt[window]=now+5000;historySource[window]=source==2?0:2;}
    else nextQuoteAt=now+5000;
    return; // Local RAM contention is not an exchange failure/rate limit.
  }
  bool emptyCandles=result.request.kind==market::Kind::Candles;
  if(emptyCandles)for(unsigned w=0;w<5;++w)if((result.candleAttemptMask&(1U<<w))&&result.candles[w].count(w,chartNowMs()))emptyCandles=false;
  if (!result.ok||emptyCandles) {
    rotation.failure(source,now,result.status,result.retryAfterMs);
    Serial.printf("%s %s failed (HTTP %d), cooling down\n",btc::kSources[source].name,result.request.kind==market::Kind::Quote?"ticker":"history",result.status);
    if (isHistory(result.request.kind)) {
      uint8_t window=result.request.window;
      historySource[window]=source==2?0:2;
      historyErrors[window]=std::min<uint32_t>(historyErrors[window]+1,5);
      nextHistoryAt[window]=now+(historyErrors[window]==1?1000:std::min<uint32_t>(900000,30000U<<(historyErrors[window]-2)));
    } else nextQuoteAt=now+500;
    return;
  }
  rotation.success(source);
  if (isHistory(result.request.kind)) {
    uint32_t epoch=uint32_t(time(nullptr));
    if(result.request.kind==market::Kind::Candles){
      uint64_t utcMs=chartNowMs();
      // One transaction response builds each requested range independently.
      // Merge into each retained cache, including ranges not currently selected.
      for(unsigned w=0;w<5;++w)if(result.candleAttemptMask&(1U<<w)){
        if(candleSeries[w].merge(result.candles[w],w,utcMs))markCandleDirty(w,now);
        uint32_t latest=candleSeries[w].latestCloseCents(w);if(latest)candleReferencePrice[w]=latest;
        historyErrors[w]=0;nextHistoryAt[w]=now+config::kHistoryRefreshMs[w];historySource[w]=2;
        if(candleScales[w].update(candleSeries[w],w,utcMs))markCandleDirty(w,now);
      }
      if(result.quote.price&&!guard.price&&btc::freshTimestamp(result.quote.timestamp,epoch))acceptQuote(result.quote,uint8_t(source),result.receivedAt,epoch,now);
      for(unsigned w=0;w<5;++w)if((result.candleCoverageMask&(1U<<w))&&result.candles[w].count(w,utcMs))candleBackfilled[w]=true;
      Serial.printf("%s: OHLC caches merged, attempted=%02x complete=%02x selected=%s\n",btc::kSources[source].name,result.candleAttemptMask,result.candleCoverageMask,config::kWindowNames[timelineDial.stable]);
      nextDrawAt=now;return;
    }
    return; // Close-only legacy responses never fabricate candle highs/lows.
  } else {
    if (source==1 && result.quote.usdtUsd>=0.5 && result.quote.usdtUsd<=1.5) { fx.usd=result.quote.usdtUsd; fx.receivedAt=result.receivedAt; }
    if (result.request.kind==market::Kind::Fx && guard.price) return;
    acceptQuote(result.quote,uint8_t(source),result.receivedAt,result.epoch,now);
    if (needConfirmation) Serial.println("Price awaiting an independent confirmation");
    else Serial.printf("%s: BTC/USD %.2f%s\n",btc::kSources[source].name,guard.price,btc::kSources[source].usdt?" (converted from USDT)":"");
  }
}
bool dispatch(market::Kind kind,uint8_t source,uint8_t window,uint32_t now,uint32_t epoch,uint32_t start=0) {
  if (!market::send({kind,source,window,epoch,start})) return false;
  live::httpBusy(true);
  httpKind=uint8_t(kind);httpSource=source;
  rotation.started(source,now); inflight=true; return true;
}
void schedule(uint32_t now) {
  if (!networkReady || inflight || !wifi.online()) return;
  uint32_t epoch=uint32_t(time(nullptr));
  if (epoch<config::kMinimumEpoch) return;
  // A first stream quote lights the tubes immediately. Try a direct USD HTTP
  // ticker immediately before starting streams; FX must not block the first
  // USD price. A competing stream handshake must not strand initial display.
  // After one feed starts, give the second time to establish without history
  // or confirmation HTTP repeatedly reclaiming its TLS allocation.
  if (!streamWarmupStarted) {
    streamWarmupStarted=true;initialOtaGraceUntil=streamWarmupUntil=now+35000;firstQuoteFallbackAt=now;
  }
  const uint8_t window=timelineDial.stable;
  if(forceOtaPending&&ota::bootConfirmed()&&market::send({market::Kind::Ota,0,window,epoch})){
    httpKind=uint8_t(market::Kind::Ota);httpSource=0;
    forceOtaPending=false;ota::defer(now);inflight=true;live::httpBusy(true);return;
  }
  // An unavailable market must not prevent the device from receiving an OTA
  // fix. Only the bounded initial stream grace precedes manifest checks.
  if (btc::due(now,initialOtaGraceUntil) && ota::checkDue(now,wifi.online()) && market::send({market::Kind::Ota,0,window,epoch})) {
    httpKind=uint8_t(market::Kind::Ota);httpSource=0;
    ota::defer(now);inflight=true;live::httpBusy(true);return;
  }
  if(liveReady&&guard.price&&(!live::fresh(now)||live::status().connections<2)&&!btc::due(now,streamWarmupUntil))return;
  if (!guard.confirmed()) {
    if (liveReady && !btc::due(now,guard.price?streamWarmupUntil:firstQuoteFallbackAt)) return;
    if (btc::due(now,nextQuoteAt)) {
      if(window<2&&!guard.price&&btc::due(now,rotation.states[2].availableAt)&&dispatch(market::Kind::Candles,2,window,now,epoch,epoch-config::kWindowSeconds[window])){nextQuoteAt=now+1000;return;}
      int source=rotation.choose(now,false);
      if (source>=0 && dispatch(market::Kind::Quote,uint8_t(source),timelineDial.stable,now,epoch))
        nextQuoteAt=now+config::kRefreshMs[refreshDial.stable];
      else nextQuoteAt=now+500;
    }
    return;
  }
  if (guard.price && btc::due(now,nextHistoryAt[window])) {
    uint64_t utcMs=chartNowMs();
    if(candleSeries[window].trim(window,utcMs))markCandleDirty(window,now);
    bool continuous=chartContinuousSince&&utcMs>=chartContinuousSince+uint64_t(config::kWindowSeconds[window])*1000&&live::fresh(now);
    if(continuous||(candleBackfilled[window]&&live::fresh(now)))nextHistoryAt[window]=now+config::kHistoryRefreshMs[window];
    else{
      uint8_t source=historySource[window];if(!btc::due(now,rotation.states[source].availableAt))source=source==2?0:2;
      if(btc::due(now,rotation.states[source].availableAt)&&dispatch(market::Kind::Candles,source,window,now,epoch,epoch-config::kWindowSeconds[window]))return;
    }
  }
  // Stablecoin conversion is refreshed separately from the display frequency.
  if (!live::fresh(now) && (!fx.fresh(now) || uint32_t(now-fx.receivedAt)>=config::kFxRefreshMs) && btc::due(now,rotation.states[1].availableAt)) {
    if (dispatch(market::Kind::Fx,1,window,now,epoch)) return;
  }
  // Healthy streams already deliver actual market data. Display estimates
  // supply visual motion when those real prices repeat; identical prices
  // must not cause REST handshakes to reclaim a second feed's TLS allocation.
  bool liveFresh=live::fresh(now);
  if (btc::due(now,nextQuoteAt) && (needConfirmation || !liveFresh)) {
    int source=rotation.choose(now,fx.fresh(now));
    if (source>=0 && dispatch(market::Kind::Quote,uint8_t(source),window,now,epoch)) nextQuoteAt=now+config::kRefreshMs[refreshDial.stable];
    else nextQuoteAt=now+500;
  }
}
void draw(uint32_t now) {
  bool setupDisplay=wifi.portalActive()||!wifi.online();
  if (setupDisplay!=showingSetup) { showingSetup=setupDisplay; nextDrawAt=now; }
  if (!btc::due(now,nextDrawAt)) return;
  nextDrawAt=now+(setupDisplay?config::kSetupFrameMs:display::startupAnimating()?config::kStartupFrameMs:1000);
  if (testEndsAt && !btc::due(now,testEndsAt)) { display::selfTest(uint8_t((4500-(testEndsAt-now))/750)); return; }
  testEndsAt=0;
  const bool testingTubes=tubeTestEndsAt && !btc::due(now,tubeTestEndsAt);
  if (setupDisplay) {
    display::setupStatus(wifi.setupState(),now);
    if (!testingTubes&&!guard.price) display::setupZero(now);
    return;
  }
  if(matrixDiagnostics){
    char top[24],bottom[24];
    if(diagnosticPage==0){
      const char* frequency[]={"F.1S","F.5S","F1.5S","F10S","F1M"};
      const char* range[]={"R30S","R1M","R5M","R30M","R1H"};
      snprintf(top,sizeof(top),"%s%s",frequency[refreshDial.stable],refreshDial.initialized&&frequencyDecoded==refreshDial.stable?"":"?");
      snprintf(bottom,sizeof(bottom),"%s%s",range[timelineDial.stable],timelineDial.initialized&&rangeDecoded==timelineDial.stable?"":"?");
    }else if(diagnosticPage==1){snprintf(top,sizeof(top),"F%04u",frequencyMv);snprintf(bottom,sizeof(bottom),"R%04u",rangeMv);}
    else if(diagnosticPage==2){snprintf(top,sizeof(top),"Q%04lu",(unsigned long)(acceptedQuotes%10000));snprintf(bottom,sizeof(bottom),"N%04lu",(unsigned long)(nixieFrames%10000));}
    else if(diagnosticPage==3){auto ip=WiFi.localIP();unsigned offset=(now/2000)%2?2:0;snprintf(top,sizeof(top),"%u",ip[offset]);snprintf(bottom,sizeof(bottom),"%u",ip[offset+1]);}
    else {snprintf(top,sizeof(top),"TUBE");snprintf(bottom,sizeof(bottom),testingTubes?"TEST":"DONE");}
    display::message(top,bottom,now);nextDrawAt=now+100;return;
  }
  uint64_t utcMs=chartNowMs();
  if(utcMs<uint64_t(config::kMinimumEpoch)*1000)utcMs=candleSavedTime[timelineDial.stable];
  unsigned window=timelineDial.stable;
  if(candleScales[window].update(candleSeries[window],window,utcMs))markCandleDirty(window,now);
  auto frame=btc::makeCandles(candleSeries[window],candleScales[window],window,utcMs,candleReferencePrice[window]);
  if(frame.valid){
    // All chart pixels are continuously on at the same driver intensity.
    auto chart=frame.plot();chartColumns=frame.valid;chartPixels=0;
    for(unsigned col=0;col<21;++col)for(unsigned row=0;row<13;++row)chartPixels+=btc::plotPixel(chart,row,col);
    display::startupPlot(chart,now);
    uint64_t boundary=btc::candleStart(btc::candleBucket(utcMs,window)+1,window);
    nextDrawAt=now+(display::startupAnimating()?config::kStartupFrameMs:uint32_t(std::min<uint64_t>(1000,std::max<uint64_t>(1,boundary-utcMs))));
  }else{
    chartColumns=0;chartPixels=0;
    if(utcMs<uint64_t(config::kMinimumEpoch)*1000)display::message("SYNC","CLOCK",now);
    else if(!guard.price)display::message("FETCH","PRICE",now);
    else display::message("FETCH","CHART",now);
  }
}

void saveHistory(uint32_t now) {
  uint64_t utcMs=chartNowMs();
  if(!inflight&&candleStore.ready()&&utcMs>=uint64_t(config::kMinimumEpoch)*1000)for(unsigned w=0;w<5;++w){
    if(candleSeries[w].trim(w,utcMs))markCandleDirty(w,now);
    if(candleDirty[w]&&btc::due(now,nextCandleSave[w])){
      candleScales[w].update(candleSeries[w],w,utcMs);
      if(!candleSeries[w].count(w,utcMs)){candleDirty[w]=false;nextCandleSave[w]=0;continue;}
      if(market::sendCache(candleStore,w,candleSeries[w],candleScales[w],utcMs)){
        // Subsequent live ticks dirty the next checkpoint while this immutable
        // copy saves on core 0. Only this loop publishes committed cache dates.
        candleDirty[w]=false;nextCandleSave[w]=now+config::kShortHistorySaveMs;
        inflight=true;httpKind=uint8_t(market::Kind::SaveCache);httpSource=255;
      }
      break;
    }
  }
}
#ifdef BTC_SKIT_DEMO
bool skitStarted=false;
uint32_t skitStart=0;
void restartSkit(uint32_t now){
  skitStarted=true;skitStart=now;nextNixieAt=now;
  skit::seedChart(candleSeries[skit::kWindow],candleScales[skit::kWindow]);
}
void skitLoop(uint32_t now){
  wifi.loop(now);remoteCommands(now);serialCommands(now);
  refreshDial.stable=1;timelineDial.stable=skit::kWindow;
  refreshDial.initialized=timelineDial.initialized=true;
  if(!skitStarted)restartSkit(now);
  // A short BOOT tap replays a take; long holds retain Wi-Fi setup behavior.
  static uint32_t pressed=0;
  if(digitalRead(0)==LOW){if(!pressed)pressed=now;}
  else if(pressed){if(uint32_t(now-pressed)>=40&&uint32_t(now-pressed)<1000)restartSkit(now);pressed=0;}
  if(wifi.online()&&!timeStarted){configTime(0,0,"time.cloudflare.com","pool.ntp.org");timeStarted=true;}
  processResult(now);live::pauseAndWait(0);
  uint32_t elapsed=now-skitStart;
  if(elapsed>=skit::kCycleMs){restartSkit(now);elapsed=0;}
  uint64_t stamp=skit::kChartStart+elapsed;
  if(btc::due(now,nextNixieAt)){
    uint32_t period=skit::kRefreshMs;
    shownDigits=skit::digits(elapsed);shownPrice=shownDigits;displayEstimated=true;++estimatedFrames;
    display::price(shownDigits,true);
    lastFrameGap=nixieFrames?now-lastNixieAt:0;
    if(nixieFrames){minimumFrameGap=minimumFrameGap?std::min(minimumFrameGap,lastFrameGap):lastFrameGap;maximumFrameGap=std::max(maximumFrameGap,lastFrameGap);}
    lastFramePeriod=period;lastNixieAt=now;++nixieFrames;
    nextNixieAt+=((now-nextNixieAt)/period+1)*period;
    candleSeries[skit::kWindow].add(stamp,skit::price(elapsed),skit::kWindow,stamp);
    candleScales[skit::kWindow].update(candleSeries[skit::kWindow],skit::kWindow,stamp);
    auto frame=btc::makeCandles(candleSeries[skit::kWindow],candleScales[skit::kWindow],skit::kWindow,stamp);
    auto chart=frame.plot();display::plot(chart);chartColumns=frame.valid;chartPixels=0;
    for(unsigned c=0;c<21;++c)for(unsigned r=0;r<13;++r)chartPixels+=btc::plotPixel(chart,r,c);
  }
  // Only OTA uses the HTTP worker; simulated data never reaches flash caches.
  uint32_t epoch=uint32_t(time(nullptr));
  if(networkReady&&!inflight&&wifi.online()&&epoch>=config::kMinimumEpoch&&
     ((forceOtaPending&&ota::bootConfirmed())||ota::checkDue(now,true))&&
     market::send({market::Kind::Ota,0,skit::kWindow,epoch})){
    forceOtaPending=false;ota::defer(now);inflight=true;httpKind=uint8_t(market::Kind::Ota);httpSource=0;
  }
  ota::healthCheck(now,networkReady&&wifi.online()&&nixieFrames>0&&ESP.getFreeHeap()>30000);
  publishTelemetry(now);delay(2);
}
#endif

}
void setup() {
  // Queue a complete diagnostic report instead of waiting for each byte to
  // drain over USB; status logging must not hold up the display timer.
  Serial.setTxBufferSize(2048);
  Serial.begin(115200); serialLine.reserve(64);
  Serial.printf("Firmware boot: %s\n",ota_config::kVersion);
  display::begin();
  #ifndef BTC_SKIT_DEMO
  candleStore.begin(candleSeries,candleScales);
  #endif
  for(unsigned w=0;w<5;++w){candleSavedTime[w]=candleStore.savedTimeMs(w);candleReferencePrice[w]=candleSeries[w].latestCloseCents(w);}
  pinMode(0,INPUT_PULLUP); pinMode(config::kRefreshPin,INPUT); pinMode(config::kTimelinePin,INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(config::kRefreshPin,ADC_11db); analogSetPinAttenuation(config::kTimelinePin,ADC_11db);
  Serial.println("Bitcoin Clock live firmware. Type status or dials; hold BOOT for 5 seconds to set up Wi-Fi.");
  wifi.begin(); networkReady=market::begin(); ota::begin();
  liveReady=live::begin();
#ifdef BTC_SKIT_DEMO
  live::pauseAndWait(1000);
#endif
  // Let the first USD HTTP request use the full TLS pool. processResult
  // releases this hold on either success or failure, then streams take over.
  if(networkReady)live::httpBusy(true);
  if(!telemetry::begin())Serial.println("Wi-Fi telemetry worker unavailable.");
  if (!liveReady) Serial.println("Live stream worker unavailable; using REST fallback.");
  if (!networkReady) Serial.println("HTTP worker allocation failed; reboot to retry.");
}
void loop() {
#ifdef BTC_SKIT_DEMO
  skitLoop(millis());return;
#endif
  uint32_t now=millis();
  wifi.loop(now); readDials(now); remoteCommands(now);serialCommands(now);
  if (wifi.online()&&!timeStarted) { configTime(0,0,"time.cloudflare.com","pool.ntp.org","time.google.com"); timeStarted=true; }
  processResult(now); processLive(now);now=millis();refreshNixies(now);draw(now);saveHistory(now);schedule(now);
  ota::healthCheck(now,networkReady&&wifi.online()&&guard.price>0&&ESP.getFreeHeap()>30000);
  publishTelemetry(millis());
  delay(2);
}
