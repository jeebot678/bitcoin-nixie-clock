#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "ClockCore.h"
#include "Display.h"
#include "MarketClient.h"
#include "Provisioning.h"
#include "OtaUpdate.h"
#include "OtaConfig.h"
#include "HistoryStore.h"
#include "LivePrice.h"
#include "QuoteBook.h"
#include "DeviceTelemetry.h"

namespace {
Provisioning wifi;
// An unread/unstable selector must not silently leave the price at 30 seconds.
btc::Dial refreshDial(0),timelineDial;
btc::Rotation rotation;
btc::FxRate fx;
btc::PriceGuard guard;
btc::QuoteBook quotes;
btc::History histories[5];
HistoryStore historyStore;
bool historyDirty[5] = {};
uint32_t nextSaveAt[5] = {};
market::Result result;
bool networkReady=false,inflight=false,timeStarted=false;
bool liveReady=false,streamWarmupStarted=false;
uint32_t streamWarmupUntil=0,firstQuoteFallbackAt=0;
bool showingSetup=false;
bool needConfirmation=true,logNixies=false;
double shownPrice=0;
uint8_t shownSource=0,lastAcceptedSource=0;
uint32_t shownQuoteReceived=0;
uint32_t shownDigits=0;
uint32_t nextNixieAt=0,nixieFrames=0,lastNixieAt=0,acceptedQuotes=0;
uint32_t lastFrameGap=0,lastFramePeriod=0,minimumFrameGap=0,maximumFrameGap=0;
uint32_t nextTelemetryAt=0,nextRawAt=0;
uint16_t frequencyMv=0,rangeMv=0,frequencyRaw=0,rangeRaw=0;
int frequencyDecoded=-1,rangeDecoded=-1;
bool forceOtaPending=false,matrixDiagnostics=false;
uint8_t diagnosticPage=0;
uint32_t nextQuoteAt=0,nextDialsAt=0,nextDrawAt=0,lastPriceEpoch=0,lastQuoteReceived=0;
uint32_t nextHistoryAt[5]={},historyErrors[5]={};
uint8_t historySource[5]={1,1,1,2,1}; // Bitstamp's day JSON uses less heap.
uint32_t bootPressedAt=0,testEndsAt=0,tubeTestEndsAt=0;
bool bootHandled=false;
String serialLine;

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
    Serial.printf("Range JSEL1/GPIO34 position %d: %s (%u cached candles)\n",timelineDial.stable+1,config::kWindowNames[timelineDial.stable],histories[timelineDial.stable].count);
  }
}
void remoteCommands(uint32_t now) {
  telemetry::Command command;
  for(unsigned i=0;i<4&&telemetry::receive(command);++i){
    switch(command){
      case telemetry::Command::Ota:forceOtaPending=true;break;
      case telemetry::Command::Tubes:tubeTestEndsAt=now+5000;break;
      case telemetry::Command::Readings:matrixDiagnostics=true;diagnosticPage=1;nextDrawAt=now;break;
      case telemetry::Command::Chart:matrixDiagnostics=false;tubeTestEndsAt=0;nextDrawAt=now;break;
    }
  }
}
void publishTelemetry(uint32_t now) {
  if(!btc::due(now,nextTelemetryAt))return;
  nextTelemetryAt=now+250;
  telemetry::Snapshot s;s.uptime=now;s.heap=ESP.getFreeHeap();s.largest=ESP.getMaxAllocHeap();
  s.online=wifi.online();s.portal=wifi.portalActive();s.clock=time(nullptr)>=config::kMinimumEpoch;
  s.frequency.mv=frequencyMv;s.frequency.raw=frequencyRaw;s.frequency.decoded=frequencyDecoded;
  s.frequency.selected=refreshDial.stable;s.frequency.initialized=refreshDial.initialized;
  s.range.mv=rangeMv;s.range.raw=rangeRaw;s.range.decoded=rangeDecoded;
  s.range.selected=timelineDial.stable;s.range.initialized=timelineDial.initialized;
  s.interval=config::kRefreshMs[refreshDial.stable];s.window=config::kWindowSeconds[timelineDial.stable];
  auto liveState=live::status();s.connections=liveState.connections;s.quotes=liveState.quotes;
  s.accepted=acceptedQuotes;s.frames=nixieFrames;s.quoteAge=now-lastQuoteReceived;s.frameAge=now-lastNixieAt;
  s.frameGap=lastFrameGap;s.nextFrame=uint32_t(std::max<int32_t>(0,int32_t(nextNixieAt-now)));
  s.minimumGap=minimumFrameGap;s.maximumGap=maximumFrameGap;
  s.marketPrice=guard.price;s.displayPrice=shownPrice;s.digits=shownDigits;s.source=shownSource;
  bool testing=tubeTestEndsAt&&!btc::due(now,tubeTestEndsAt);
  s.displayMode=testing?3:!s.online||!guard.price?1:uint32_t(now-lastQuoteReceived)>btc::staleAfterMs(s.interval)?2:0;
  if(testing)s.testStep=uint8_t((5000-uint32_t(tubeTestEndsAt-now))/500);
  s.matrixPage=matrixDiagnostics?diagnosticPage:255;
  s.confirmed=guard.confirmed();s.inflight=inflight;s.otaPending=forceOtaPending;s.bootConfirmed=ota::bootConfirmed();telemetry::publish(s);
}
void diagnostics(uint32_t now) {
  Serial.printf("Firmware=%s Wi-Fi=%s portal=%s setup=%s clock=%s heap=%u largest=%u refresh=%lu window=%s price=%.2f stale=%s fx=%.6f fxFresh=%s history=%u\n",
    ota_config::kVersion,wifi.online()?"connected":"offline",wifi.portalActive()?"on":"off",btc::setupStateName(wifi.setupState()),time(nullptr)>=config::kMinimumEpoch?"synced":"waiting",
    ESP.getFreeHeap(),ESP.getMaxAllocHeap(),(unsigned long)config::kRefreshMs[refreshDial.stable],config::kWindowNames[timelineDial.stable],guard.price,
    !guard.price||uint32_t(now-lastQuoteReceived)>btc::staleAfterMs(config::kRefreshMs[refreshDial.stable])?"yes":"no",fx.usd,fx.fresh(now)?"yes":"no",histories[timelineDial.stable].count);
  Serial.printf("  History storage=%s savedEpoch=%lu dirty=%s\n",historyStore.ready()?"ready":"unavailable",(unsigned long)historyStore.savedEpoch(timelineDial.stable),historyDirty[timelineDial.stable]?"yes":"no");
  for (uint8_t window=0;window<5;++window) Serial.printf("  Cache %s: %u candles, savedEpoch=%lu\n",config::kWindowNames[window],histories[window].count,(unsigned long)historyStore.savedEpoch(window));
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
    else if (!serialLine.isEmpty()) Serial.println("Commands: status, dials, ticks, tube-test, readings, ota-check, setup, wifi-reset, selftest, reboot");
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
  if (!fx.normalize(quote.price,btc::kSources[source].usdt,now,price)) {nextQuoteAt=now+500;return;}
  if (guard.accept(price,source,now)) {
    lastPriceEpoch=quote.timestamp?quote.timestamp:epoch;
    lastQuoteReceived=receivedAt;++acceptedQuotes;needConfirmation=!guard.confirmed();
    if (needConfirmation) nextQuoteAt=now; // Confirmation never waits for a slow display dial.
    lastAcceptedSource=source;quotes.put(price,source,receivedAt,lastPriceEpoch);
    nextDrawAt=now;
  } else {needConfirmation=true;nextQuoteAt=now;}
}
void processLive(uint32_t now) {
  live::Tick tick;
  for (unsigned i=0;i<2 && live::receive(tick);++i) {
    now=millis();
    if (tick.source<btc::kSourceCount && wifi.online() &&
      uint32_t(now-tick.receivedAt)<=5000 && btc::freshTimestamp(tick.quote.timestamp,uint32_t(time(nullptr))))
    acceptQuote(tick.quote,tick.source,tick.receivedAt,tick.quote.timestamp,now);
  }
}
void refreshNixies(uint32_t now) {
  if (tubeTestEndsAt && !btc::due(now,tubeTestEndsAt)) {
    display::testDigits(uint8_t((5000-uint32_t(tubeTestEndsAt-now))/500));
    shownPrice=0;nextNixieAt=now;return;
  }
  tubeTestEndsAt=0;
  if (!wifi.online()) {quotes.clear();shownPrice=0;nextNixieAt=now;return;}
  if (!guard.price) {display::setupZero(now);shownPrice=0;return;}
  if (uint32_t(now-lastQuoteReceived)>btc::staleAfterMs(config::kRefreshMs[refreshDial.stable])) {
    display::blankPrice();shownPrice=0;return;
  }
  if (!shownPrice || btc::due(now,nextNixieAt)) {
    const bool firstFrame=!shownPrice;
    const uint32_t period=config::kRefreshMs[refreshDial.stable];
    btc::DisplayQuote selected{guard.price,lastQuoteReceived,lastPriceEpoch,lastAcceptedSource};
    quotes.choose(now,firstFrame?0:shownDigits,shownSource,guard.price,fx.fresh(now),selected);
    bool cosmetic=false;
    // Keep the explicitly cosmetic fast-mode effect running while a real
    // quote is held through a brief reconnect. Its receipt timestamp stays
    // intact; the ordinary stale check above still blanks an actual outage.
    shownDigits=btc::displayDollars(selected.price,shownDigits,!firstFrame && period==500,cosmetic);
    display::price(double(shownDigits),true);shownPrice=selected.price;shownSource=selected.source;shownQuoteReceived=selected.receivedAt;++nixieFrames;
    lastFrameGap=now-lastNixieAt;
    if(lastFramePeriod!=period||firstFrame){minimumFrameGap=maximumFrameGap=0;lastFramePeriod=period;}
    else {if(!minimumFrameGap||lastFrameGap<minimumFrameGap)minimumFrameGap=lastFrameGap;if(lastFrameGap>maximumFrameGap)maximumFrameGap=lastFrameGap;}
    if (logNixies || nixieFrames==1) Serial.printf("Nixie frame at %lu ms: %.2f interval=%lu ms gap=%lu ms quoteAge=%lu ms source=%s digits=%lu cosmetic=%s\n",(unsigned long)now,shownPrice,
      (unsigned long)config::kRefreshMs[refreshDial.stable],(unsigned long)uint32_t(now-lastNixieAt),(unsigned long)uint32_t(now-shownQuoteReceived),btc::kSources[shownSource].name,(unsigned long)shownDigits,cosmetic?"yes":"no");
    lastNixieAt=now;
    // Preserve the timer phase through normal loop jitter. If work delays a
    // frame, send the latest value once and skip expired deadlines.
    nextNixieAt=firstFrame?now+period:nextNixieAt+(uint32_t(now-nextNixieAt)/period+1)*period;
  }
}
void processResult(uint32_t now) {
  if (!market::receive(result)) return;
  now=millis();
  inflight=false;
  live::httpBusy(false);
  if (result.request.kind==market::Kind::Ota) { nextQuoteAt=now;return; }
  size_t source=result.request.source;
  if (!result.ok) {
    rotation.failure(source,now,result.status,result.retryAfterMs);
    Serial.printf("%s %s failed (HTTP %d), cooling down\n",btc::kSources[source].name,result.request.kind==market::Kind::Quote?"ticker":"history",result.status);
    if (result.request.kind==market::Kind::History) {
      uint8_t window=result.request.window;
      historySource[window]=source==1?2:1;
      historyErrors[window]=std::min<uint32_t>(historyErrors[window]+1,5);
      nextHistoryAt[window]=now+(historyErrors[window]==1?1000:std::min<uint32_t>(900000,30000U<<(historyErrors[window]-2)));
    } else nextQuoteAt=now+500;
    return;
  }
  rotation.success(source);
  if (result.request.kind==market::Kind::History) {
    uint8_t window=result.request.window;
    uint32_t epoch=uint32_t(time(nullptr));
    if (btc::mergeHistory(histories[window],result.history,window,epoch)) historyDirty[window]=true;
    // The three short ranges share one-minute candles. Reuse fetched minutes
    // across those caches instead of downloading them again for another detent.
    if (window<3) for (uint8_t other=0;other<3;++other) if (other!=window &&
        btc::mergeHistory(histories[other],result.history,other,epoch)) historyDirty[other]=true;
    // Recheck at the next candle boundary, independent of the quote-frequency dial.
    uint32_t interval=config::kCandleSeconds[window];
    bool missing=btc::missingHistoryStart(histories[window],window,epoch)!=0;
    nextHistoryAt[window]=now+(missing?1000:(interval-epoch%interval)*1000); historyErrors[window]=0;
    if (missing) historySource[window]=source==1?2:1;
    Serial.printf("%s: %u closed candles received for %s; %u cached\n",btc::kSources[source].name,result.history.count,config::kWindowNames[window],histories[window].count);
    nextDrawAt=now;
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
  rotation.started(source,now); inflight=true; return true;
}
void schedule(uint32_t now) {
  if (!networkReady || inflight || !wifi.online()) return;
  uint32_t epoch=uint32_t(time(nullptr));
  if (epoch<config::kMinimumEpoch) return;
  // A first stream quote lights the tubes immediately. Try a direct USD HTTP
  // ticker after 5 s if none arrived; FX must not block the first USD price.
  // After one feed starts, give the second time to establish without history
  // or confirmation HTTP repeatedly reclaiming its TLS allocation.
  if (!streamWarmupStarted) {
    streamWarmupStarted=true;streamWarmupUntil=now+35000;firstQuoteFallbackAt=now+5000;
  }
  const uint8_t window=timelineDial.stable;
  if(forceOtaPending&&ota::bootConfirmed()&&market::send({market::Kind::Ota,0,window,epoch})){
    forceOtaPending=false;ota::defer(now);inflight=true;live::httpBusy(true);return;
  }
  // An unavailable market must not prevent the device from receiving an OTA
  // fix. Only the bounded initial stream grace precedes manifest checks.
  if (btc::due(now,streamWarmupUntil) && ota::checkDue(now,wifi.online()) && market::send({market::Kind::Ota,0,window,epoch})) {
    ota::defer(now);inflight=true;live::httpBusy(true);return;
  }
  if (!guard.confirmed()) {
    if (liveReady && !btc::due(now,guard.price?streamWarmupUntil:firstQuoteFallbackAt)) return;
    if (btc::due(now,nextQuoteAt)) {
      int source=rotation.choose(now,false);
      if (source>=0 && dispatch(market::Kind::Quote,uint8_t(source),timelineDial.stable,now,epoch))
        nextQuoteAt=now+config::kRefreshMs[refreshDial.stable];
      else nextQuoteAt=now+500;
    }
    return;
  }
  if (guard.price && btc::due(now,nextHistoryAt[window])) {
    btc::trimHistory(histories[window],window,epoch);
    uint32_t start=btc::missingHistoryStart(histories[window],window,epoch);
    if (!start) {
      uint32_t interval=config::kCandleSeconds[window];
      nextHistoryAt[window]=now+(interval-epoch%interval)*1000;
    } else {
      uint8_t source=historySource[window];
      if (!btc::due(now,rotation.states[source].availableAt)) source=source==1?2:1;
      if (btc::due(now,rotation.states[source].availableAt) && dispatch(market::Kind::History,source,window,now,epoch,start)) {
        Serial.printf("History fetch: %s, missing from %lu (cached=%u)\n",config::kWindowNames[window],(unsigned long)(start+config::kCandleSeconds[window]),histories[window].count);
        return;
      }
    }
  }
  // Stablecoin conversion is refreshed separately from the display frequency.
  if (!live::fresh(now) && (!fx.fresh(now) || uint32_t(now-fx.receivedAt)>=config::kFxRefreshMs) && btc::due(now,rotation.states[1].availableAt)) {
    if (dispatch(market::Kind::Fx,1,window,now,epoch)) return;
  }
  if (btc::due(now,nextQuoteAt) && (needConfirmation || !live::fresh(now))) {
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
  bool stale=!guard.price || uint32_t(now-lastQuoteReceived)>btc::staleAfterMs(config::kRefreshMs[refreshDial.stable]);
  const bool testingTubes=tubeTestEndsAt && !btc::due(now,tubeTestEndsAt);
  if (setupDisplay) {
    display::setupStatus(wifi.setupState(),now);
    if (!testingTubes) {
      if (!wifi.online()) display::setupZero(now);
      else if (stale && guard.price) display::blankPrice();
    }
    return;
  }
  if (stale && guard.price && !testingTubes) display::blankPrice();
  if(matrixDiagnostics){
    char top[24],bottom[24];
    if(diagnosticPage==0){
      const char* frequency[]={"F.5S","F2S","F30S","F5M","F1H"};
      const char* range[]={"R5M","R30M","R1H","R24H","R7D"};
      snprintf(top,sizeof(top),"%s%s",frequency[refreshDial.stable],refreshDial.initialized&&frequencyDecoded==refreshDial.stable?"":"?");
      snprintf(bottom,sizeof(bottom),"%s%s",range[timelineDial.stable],timelineDial.initialized&&rangeDecoded==timelineDial.stable?"":"?");
    }else if(diagnosticPage==1){snprintf(top,sizeof(top),"F%04u",frequencyMv);snprintf(bottom,sizeof(bottom),"R%04u",rangeMv);}
    else if(diagnosticPage==2){snprintf(top,sizeof(top),"Q%04lu",(unsigned long)(acceptedQuotes%10000));snprintf(bottom,sizeof(bottom),"N%04lu",(unsigned long)(nixieFrames%10000));}
    else if(diagnosticPage==3){auto ip=WiFi.localIP();unsigned offset=(now/2000)%2?2:0;snprintf(top,sizeof(top),"%u",ip[offset]);snprintf(bottom,sizeof(bottom),"%u",ip[offset+1]);}
    else {snprintf(top,sizeof(top),"TUBE");snprintf(bottom,sizeof(bottom),testingTubes?"TEST":"DONE");}
    display::message(top,bottom,now);nextDrawAt=now+100;return;
  }
  uint32_t epoch=uint32_t(time(nullptr));
  // A bare ESP32 cannot know how long it was off until SNTP succeeds. Before
  // then, show the saved dated snapshot; never use it as the TLS/current clock.
  if (epoch<config::kMinimumEpoch) epoch=historyStore.savedEpoch(timelineDial.stable);
  const auto& history=histories[timelineDial.stable];
  uint32_t window=config::kWindowSeconds[timelineDial.stable];
  auto chart=btc::makePlot(history,epoch,window,guard.price,lastPriceEpoch);
  // Wait for dated history on the first boot rather than revealing a lone
  // live quote and fetching the selected chart only after the animation ends.
  bool waitingForHistory=display::startupPending() && !btc::makePlot(history,epoch,window,0,0).valid;
  if (chart.valid && !waitingForHistory) {
    display::startupPlot(chart,now);
    nextDrawAt=now+(display::startupAnimating()?config::kStartupFrameMs:1000);
  }
  else if (epoch<config::kMinimumEpoch) display::message("SYNC","CLOCK",now);
  else if (!guard.price) display::message("FETCH","PRICE",now);
  else if (waitingForHistory) display::message("FETCH","HISTORY",now);
  else display::offline();
}
void saveHistory(uint32_t now) {
  if (!historyStore.ready()) return;
  uint32_t epoch=uint32_t(time(nullptr));
  if (epoch<config::kMinimumEpoch) return;
  for (uint8_t window=0;window<5;++window) if (historyDirty[window]&&(!nextSaveAt[window]||btc::due(now,nextSaveAt[window]))) {
    btc::trimHistory(histories[window],window,epoch);
    if (!histories[window].count || historyStore.save(window,histories[window],epoch)) { historyDirty[window]=false;nextSaveAt[window]=0; }
    else nextSaveAt[window]=now+config::kHistorySaveRetryMs;
  }
}
}
void setup() {
  // Queue a complete diagnostic report instead of waiting for each byte to
  // drain over USB; status logging must not hold up the display timer.
  Serial.setTxBufferSize(2048);
  Serial.begin(115200); serialLine.reserve(64);
  Serial.printf("Firmware boot: %s\n",ota_config::kVersion);
  display::begin();
  historyStore.begin(histories);
  pinMode(0,INPUT_PULLUP); pinMode(config::kRefreshPin,INPUT); pinMode(config::kTimelinePin,INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(config::kRefreshPin,ADC_11db); analogSetPinAttenuation(config::kTimelinePin,ADC_11db);
  Serial.println("Bitcoin Clock live firmware. Type status or dials; hold BOOT for 5 seconds to set up Wi-Fi.");
  wifi.begin(); networkReady=market::begin(); ota::begin();
  liveReady=live::begin();
  if(!telemetry::begin())Serial.println("Wi-Fi telemetry worker unavailable.");
  if (!liveReady) Serial.println("Live stream worker unavailable; using REST fallback.");
  if (!networkReady) Serial.println("HTTP worker allocation failed; reboot to retry.");
}
void loop() {
  uint32_t now=millis();
  wifi.loop(now); readDials(now); remoteCommands(now);serialCommands(now);
  if (wifi.online()&&!timeStarted) { configTime(0,0,"time.cloudflare.com","pool.ntp.org","time.google.com"); timeStarted=true; }
  processResult(now); processLive(now);now=millis();refreshNixies(now);draw(now);saveHistory(now);schedule(now);
  ota::healthCheck(now,networkReady&&wifi.online()&&guard.price>0&&ESP.getFreeHeap()>30000);
  publishTelemetry(millis());
  delay(2);
}
