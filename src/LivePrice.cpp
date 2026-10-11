#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <memory>
#include <time.h>
#include "LivePrice.h"
#include "TlsGate.h"

extern const uint8_t caBundle[] asm("_binary_data_cert_x509_crt_bundle_bin_start");
namespace {
QueueHandle_t ticks=nullptr;
std::atomic<bool> pauseRequested{false}, paused{true}, connected{false}, busy{false};
std::atomic<uint32_t> quoteCount{0}, receivedAt{0};
std::atomic<uint32_t> stackFreeBytes{0};
class StreamSocket : public WebSocketsClient {
 public:
  int httpStatus() const { return _client.cCode; }
};
std::unique_ptr<StreamSocket> socket;
btc::TlsLease openingHandshake;
btc::SourceState cooldown[2];
uint8_t source=1,nextSource=1;
bool failed=false,closing=false;
uint32_t openedAt=0,lastMessageAt=0,lastValidQuoteAt=0;
int failureStatus=0;
StaticJsonDocument<2048> document;

void closeSocket() {
  closing=true;socket.reset();closing=false;connected=false;
  openingHandshake.release();
}
void event(WStype_t type,uint8_t* payload,size_t length) {
  if (closing) return;
  uint32_t now=millis();
  if (type==WStype_CONNECTED) {
    connected=true;lastMessageAt=now;
    const char* subscription=source==1
      ? "{\"method\":\"subscribe\",\"params\":{\"channel\":\"ticker\",\"symbol\":[\"BTC/USD\"],\"event_trigger\":\"bbo\",\"snapshot\":true}}"
      : "{\"type\":\"subscribe\",\"product_ids\":[\"BTC-USD\"],\"channels\":[\"ticker\",\"heartbeat\"]}";
    socket->sendTXT(subscription);
    Serial.printf("Live stream connected: %s (BTC/USD)\n",btc::kSources[source].name);
  } else if (type==WStype_DISCONNECTED || type==WStype_ERROR) {
    // The library clears its status field before the disconnect callback, but
    // includes HTTP errors in the callback's length-delimited reason text.
    char reason[128];size_t count=std::min(length,sizeof(reason)-1);
    if (payload && count) memcpy(reason,payload,count);else count=0;
    reason[count]=0;
    const char* code=strstr(reason,"HTTP ");
    if (code) failureStatus=atoi(code+5);
    connected=false;failed=true;
  } else if (type==WStype_TEXT && length && length<=4096) {
    document.clear();
    if (deserializeJson(document,payload,length,DeserializationOption::NestingLimit(8))) return;
    lastMessageAt=now;
    if (btc::equals(document["type"],"error") ||
        (!document["success"].isNull() && document["success"]==false)) {failed=true;return;}
    live::Tick tick;
    if (btc::parseStreamQuote(source,document.as<JsonVariantConst>(),uint32_t(time(nullptr)),tick.quote)) {
      tick.source=source;tick.receivedAt=now;
      if (xQueueOverwrite(ticks,&tick)==pdTRUE) {
        receivedAt=lastValidQuoteAt=now;++quoteCount;cooldown[source].failures=0;
      }
    }
  } else if (type==WStype_PONG) lastMessageAt=now;
}
// One persistent TLS stream, with two independent USD providers for failover.
// All socket and JSON ownership remains on this worker, never the display loop.
void step() {
  uint32_t now=millis();
  // Leave the acknowledged pause state before testing the request. A caller
  // seeing paused=true can then rely on the next step observing its request.
  paused=false;
  if (pauseRequested || WiFi.status()!=WL_CONNECTED || time(nullptr)<config::kMinimumEpoch) {
    closeSocket();paused=true;return;
  }
  if (!socket) {
    // Do not overlap two expensive connection handshakes at startup/recovery.
    if (busy || ESP.getMaxAllocHeap()<65000 || ESP.getFreeHeap()<100000) return;
    int eligible=-1;
    for (uint8_t i=0;i<2;++i) {uint8_t candidate=(nextSource+i)%2;if(btc::due(now,cooldown[candidate].availableAt)){eligible=candidate;break;}}
    if (eligible<0) return;
    source=uint8_t(eligible);failed=false;failureStatus=0;openedAt=lastMessageAt=lastValidQuoteAt=now;
    Serial.printf("Stream starting: %s, heap=%u largest=%u\n",btc::kSources[source].name,ESP.getFreeHeap(),ESP.getMaxAllocHeap());
    socket.reset(new StreamSocket());socket->onEvent(event);
    socket->beginSslWithBundle(source==1?"ws.kraken.com":"ws-feed.exchange.coinbase.com",443,source==1?"/v2":"/",caBundle,"");
    socket->setReconnectInterval(5000);socket->enableHeartbeat(20000,5000,2);
  }
  if (!connected) {
    bool started=bool(openingHandshake);
    if (!openingHandshake.acquire()) return;
    if (!started) openedAt=lastMessageAt=lastValidQuoteAt=now;
    socket->loop();
    if (connected) openingHandshake.release();
  } else socket->loop();
  now=millis();
  if (failed || uint32_t(now-lastMessageAt)>35000 || uint32_t(now-lastValidQuoteAt)>120000 || (!connected && uint32_t(now-openedAt)>15000)) {
    int code=failureStatus?failureStatus:socket->httpStatus();
    auto& state=cooldown[source];state.failures=std::min<uint8_t>(state.failures+1,5);
    uint32_t delay=std::min<uint32_t>(60000,5000U<<(state.failures-1));
    if (code==429) delay=std::max<uint32_t>(delay,60000);
    if (code==401 || code==403 || code==451) delay=3600000;
    state.availableAt=now+delay;nextSource=1-source;
    Serial.printf("Live stream retry: %s HTTP %d, backoff %lu ms\n",btc::kSources[source].name,code,(unsigned long)delay);
    closeSocket();
  }
}
void worker(void*) {for(;;){step();
#ifdef ARDUINO
  stackFreeBytes=uxTaskGetStackHighWaterMark(nullptr);
#endif
  delay(5);}}
}
namespace live {
bool begin() {
  ticks=xQueueCreate(1,sizeof(Tick));
  if (!ticks) return false;
  // Cooperative TLS reads yield during waits; keep crypto work responsive.
  if (xTaskCreatePinnedToCore(worker,"btc-stream",16384,nullptr,1,nullptr,0)==pdPASS) return true;
  vQueueDelete(ticks);ticks=nullptr;return false;
}
bool receive(Tick& tick) {return ticks && xQueueReceive(ticks,&tick,0)==pdTRUE;}
Status status() {Status state;state.connected=connected;state.paused=paused;state.quotes=quoteCount;state.receivedAt=receivedAt;state.stackFreeBytes=stackFreeBytes;return state;}
bool fresh(uint32_t now) {return connected && quoteCount && uint32_t(now-receivedAt.load())<1500;}
bool pauseAndWait(uint32_t timeoutMs) {
  pauseRequested=true;
  uint32_t start=millis();
  while(!paused && uint32_t(millis()-start)<timeoutMs) delay(5);
  return paused;
}
void resume() {pauseRequested=false;}
void httpBusy(bool value) {busy=value;}
}
