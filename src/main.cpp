#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "ClockCore.h"
#include "Display.h"
#include "MarketClient.h"
#include "Provisioning.h"
#include "OtaUpdate.h"

namespace {
Provisioning wifi;
btc::Dial refreshDial,timelineDial;
btc::Rotation rotation;
btc::FxRate fx;
btc::PriceGuard guard;
btc::History histories[5];
market::Result result;
bool networkReady=false,inflight=false,timeStarted=false;
bool showingSetup=false;
uint32_t nextQuoteAt=0,nextDialsAt=0,nextDrawAt=0,lastPriceEpoch=0,lastQuoteReceived=0;
uint32_t nextHistoryAt[5]={},historyErrors[5]={};
uint8_t historySource[5]={1,1,1,1,1};
uint32_t bootPressedAt=0,testEndsAt=0;
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
  if (refreshDial.update(btc::dialPosition(millivolts(config::kRefreshPin),config::kRefreshMv),now)) {
    nextQuoteAt=now;
    Serial.printf("Refresh dial %d: %lu ms\n",refreshDial.stable+1,(unsigned long)config::kRefreshMs[refreshDial.stable]);
  }
  if (timelineDial.update(btc::dialPosition(millivolts(config::kTimelinePin),config::kTimelineMv),now)) {
    nextHistoryAt[timelineDial.stable]=now; nextDrawAt=now;
    Serial.printf("Timeline dial %d: %s\n",timelineDial.stable+1,config::kWindowNames[timelineDial.stable]);
  }
}
void diagnostics(uint32_t now) {
  Serial.printf("Wi-Fi=%s portal=%s setup=%s clock=%s heap=%u largest=%u refresh=%lu window=%s price=%.2f stale=%s fx=%.6f fxFresh=%s\n",
    wifi.online()?"connected":"offline",wifi.portalActive()?"on":"off",btc::setupStateName(wifi.setupState()),time(nullptr)>=config::kMinimumEpoch?"synced":"waiting",
    ESP.getFreeHeap(),ESP.getMaxAllocHeap(),(unsigned long)config::kRefreshMs[refreshDial.stable],config::kWindowNames[timelineDial.stable],guard.price,
    !guard.price||uint32_t(now-lastQuoteReceived)>btc::staleAfterMs(config::kRefreshMs[refreshDial.stable])?"yes":"no",fx.usd,fx.fresh(now)?"yes":"no");
  for (size_t i=0;i<btc::kSourceCount;++i)
    Serial.printf("  %s failures=%u cooldown=%ld ms\n",btc::kSources[i].name,rotation.states[i].failures,(long)std::max<int32_t>(0,int32_t(rotation.states[i].availableAt-now)));
}
void serialCommands(uint32_t now) {
  while (Serial.available()) {
    char c=char(Serial.read());
    if (c=='\r') continue;
    if (c!='\n') { if (serialLine.length()<64) serialLine+=c; continue; }
    serialLine.trim();
    if (serialLine=="status") diagnostics(now);
    else if (serialLine=="dials") Serial.printf("Refresh GPIO34=%lu mV; timeline GPIO35=%lu mV\n",(unsigned long)millivolts(34),(unsigned long)millivolts(35));
    else if (serialLine=="setup") wifi.openSetup();
    else if (serialLine=="wifi-reset") wifi.forget();
    else if (serialLine=="selftest") testEndsAt=now+4500;
    else if (serialLine=="reboot") ESP.restart();
    else if (!serialLine.isEmpty()) Serial.println("Commands: status, dials, setup, wifi-reset, selftest, reboot");
    serialLine="";
  }
  if (digitalRead(0)==LOW) {
    if (!bootPressedAt) bootPressedAt=now;
    if (!bootHandled && uint32_t(now-bootPressedAt)>=5000) { bootHandled=true; wifi.openSetup(); }
  } else { bootPressedAt=0; bootHandled=false; }
}
void processResult(uint32_t now) {
  if (!market::receive(result)) return;
  inflight=false;
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
    histories[window]=result.history;
    nextHistoryAt[window]=now+config::kHistoryRefreshMs[window]; historyErrors[window]=0;
    Serial.printf("%s: %u closed candles for %s\n",btc::kSources[source].name,result.history.count,config::kWindowNames[window]);
    nextDrawAt=now;
  } else {
    if (source==1 && result.quote.usdtUsd>=0.5 && result.quote.usdtUsd<=1.5) { fx.usd=result.quote.usdtUsd; fx.receivedAt=result.receivedAt; }
    if (result.request.kind==market::Kind::Fx && guard.price) return;
    double price;
    if (!fx.normalize(result.quote.price,btc::kSources[source].usdt,now,price)) { nextQuoteAt=now+500; return; }
    if (guard.accept(price,source,now)) {
      lastPriceEpoch=result.quote.timestamp?result.quote.timestamp:result.epoch;
      lastQuoteReceived=result.receivedAt;
      if (wifi.online()&&!wifi.portalActive()) display::price(price);
      nextDrawAt=now;
      Serial.printf("%s: BTC/USD %.2f%s\n",btc::kSources[source].name,price,btc::kSources[source].usdt?" (converted from USDT)":"");
    } else { Serial.println("Price awaiting an independent confirmation"); nextQuoteAt=now+500; }
  }
}
bool dispatch(market::Kind kind,uint8_t source,uint8_t window,uint32_t now,uint32_t epoch) {
  if (!market::send({kind,source,window,epoch})) return false;
  rotation.started(source,now); inflight=true; return true;
}
void schedule(uint32_t now) {
  if (!networkReady || inflight || !wifi.online()) return;
  uint32_t epoch=uint32_t(time(nullptr));
  if (epoch<config::kMinimumEpoch) return;
  const uint8_t window=timelineDial.stable;
  if (ota::checkDue(now,wifi.online()) && market::send({market::Kind::Ota,0,window,epoch})) {
    ota::defer(now);inflight=true;return;
  }
  if (guard.price && btc::due(now,nextHistoryAt[window])) {
    uint8_t source=historySource[window];
    if (!btc::due(now,rotation.states[source].availableAt)) source=source==1?2:1;
    if (btc::due(now,rotation.states[source].availableAt) && dispatch(market::Kind::History,source,window,now,epoch)) return;
  }
  // Stablecoin conversion is refreshed separately from the display frequency.
  if ((!fx.fresh(now) || uint32_t(now-fx.receivedAt)>=config::kFxRefreshMs) && btc::due(now,rotation.states[1].availableAt)) {
    if (dispatch(market::Kind::Fx,1,window,now,epoch)) return;
  }
  if (btc::due(now,nextQuoteAt)) {
    int source=rotation.choose(now,fx.fresh(now));
    if (source>=0 && dispatch(market::Kind::Quote,uint8_t(source),window,now,epoch)) nextQuoteAt=now+config::kRefreshMs[refreshDial.stable];
    else nextQuoteAt=now+500;
  }
}
void draw(uint32_t now) {
  bool setupDisplay=wifi.portalActive()||!wifi.online();
  if (setupDisplay!=showingSetup) { showingSetup=setupDisplay; nextDrawAt=now; }
  if (!btc::due(now,nextDrawAt)) return;
  nextDrawAt=now+(setupDisplay?config::kSetupFrameMs:1000);
  if (testEndsAt && !btc::due(now,testEndsAt)) { display::selfTest(uint8_t((4500-(testEndsAt-now))/750)); return; }
  testEndsAt=0;
  bool stale=!guard.price || uint32_t(now-lastQuoteReceived)>btc::staleAfterMs(config::kRefreshMs[refreshDial.stable]);
  if (setupDisplay) {
    display::setupStatus(wifi.setupState(),now);
    if (!wifi.online()) display::setupZero(now);
    else if (stale) display::blankPrice();
    else display::price(guard.price);
    return;
  }
  if (stale) display::blankPrice(); else display::price(guard.price);
  uint32_t epoch=uint32_t(time(nullptr));
  auto chart=btc::makePlot(histories[timelineDial.stable],epoch,config::kWindowSeconds[timelineDial.stable],guard.price,lastPriceEpoch);
  if (chart.valid) display::plot(chart);
  else if (epoch<config::kMinimumEpoch) display::message("SYNC","CLOCK",now);
  else if (!guard.price) display::message("FETCH","PRICE",now);
  else display::offline();
}
}
void setup() {
  Serial.begin(115200); serialLine.reserve(64);
  display::begin();
  pinMode(0,INPUT_PULLUP); pinMode(config::kRefreshPin,INPUT); pinMode(config::kTimelinePin,INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(config::kRefreshPin,ADC_11db); analogSetPinAttenuation(config::kTimelinePin,ADC_11db);
  Serial.println("Bitcoin Clock live firmware. Type status or dials; hold BOOT for 5 seconds to set up Wi-Fi.");
  wifi.begin(); networkReady=market::begin(); ota::begin();
  if (!networkReady) Serial.println("HTTP worker allocation failed; reboot to retry.");
}
void loop() {
  uint32_t now=millis();
  wifi.loop(now); readDials(now); serialCommands(now);
  if (wifi.online()&&!timeStarted) { configTime(0,0,"time.cloudflare.com","pool.ntp.org","time.google.com"); timeStarted=true; }
  processResult(now); schedule(now); draw(now);
  ota::healthCheck(now,networkReady&&wifi.online()&&guard.price>0&&ESP.getFreeHeap()>30000);
  delay(2);
}
