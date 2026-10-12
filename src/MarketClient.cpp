#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "MarketClient.h"
#include "OtaUpdate.h"
#include "LivePrice.h"
#include "TlsGate.h"
#include "CooperativeTlsClient.h"
#include <atomic>

extern const uint8_t caBundle[] asm("_binary_data_cert_x509_crt_bundle_bin_start");
namespace {
QueueHandle_t requests=nullptr,results=nullptr,acknowledgments=nullptr;
std::atomic<uint32_t> stackFree{0};
// The worker waits for the main loop's response lease before reusing this.
market::Result response;
class BoundedReader {
 public:
  BoundedReader(WiFiClient& stream, size_t limit) : stream_(stream), left_(limit), deadline_(millis()+12000) {}
  int read() {
    if (!left_ || btc::due(millis(),deadline_)) return -1;
    while (!stream_.available()) {
      if (!stream_.connected() || btc::due(millis(),deadline_)) return -1;
      delay(1);
    }
    int result = stream_.read();
    if (result >= 0) --left_;
    return result;
  }
  size_t readBytes(char* buffer, size_t length) {
    size_t count=0;
    while (count<length) { int value=read(); if (value<0) break; buffer[count++]=char(value); }
    return count;
  }
 private:
  WiFiClient& stream_; size_t left_; uint32_t deadline_;
};
int apiError(JsonVariantConst root) {
  if (btc::equals(root["code"],"50011") || btc::equals(root["code"],"429000")) return 429;
  for (JsonVariantConst value : root["error"].as<JsonArrayConst>()) {
    const char* text=value.as<const char*>();
    if (text && (strstr(text,"Rate limit") || strstr(text,"Throttled"))) return 429;
  }
  return 0;
}
void fetch(const market::Request& request) {
  response.ok=false;response.deferred=false; response.status=0; response.retryAfterMs=0;
  response.quote={}; response.history.count=0; response.request=request;
  if (WiFi.status()!=WL_CONNECTED || time(nullptr)<config::kMinimumEpoch) return;
  bool history=request.kind==market::Kind::History;
  if (request.source>=btc::kSourceCount || request.window>=5) return;
  uint32_t endEpoch=request.epoch;
  uint32_t historyStart=history?btc::historyRequestStart(request.window,request.epoch,request.start):0;
  if(history&&request.source==2){
    endEpoch=std::min<uint32_t>(request.epoch,historyStart+24*config::kCandleSeconds[request.window]);
  }
  size_t capacity=history?btc::historyCapacity(request.source==2,request.window,endEpoch,historyStart):2048;
  bool allowPause=request.kind==market::Kind::Ota;
  if (!live::prepareHttp(allowPause?4096:capacity,30000,false,allowPause)){response.deferred=true;return;}
  btc::TlsLease handshake;
  uint32_t waitingAt=millis();
  while (!handshake.acquire() && uint32_t(millis()-waitingAt)<15000) delay(5);
  if (!handshake){response.deferred=true;return;}
  // A stream handshake can finish and consume RAM after the first reservation.
  if (!live::prepareHttp(allowPause?4096:capacity,30000,true,allowPause)){response.deferred=true;return;}
  if (request.kind==market::Kind::Ota) {
    response.ok=ota::checkAndInstall();return;
  }
  String url;
  // Reserve the contiguous JSON pool before TLS fragments the ESP32 heap.
  // Incremental chart requests reserve only the missing range plus overlap.
  DynamicJsonDocument document(capacity);
  if (!document.capacity()) {
    Serial.printf("Market JSON allocation failed: %u free; largest %u\n",ESP.getFreeHeap(),ESP.getMaxAllocHeap());
    return;
  }
  if (!history) url=btc::kSources[request.source].url;
  else {
    uint32_t interval=config::kCandleSeconds[request.window];
    uint32_t start=btc::historyRequestStart(request.window,request.epoch,request.start);
    if (request.source==1)
      url="https://api.kraken.com/0/public/OHLC?pair=XBTUSD&interval="+String(interval/60)+"&since="+String(start-interval);
    else
      url="https://www.bitstamp.net/api/v2/ohlc/btcusd/?step="+String(interval)+"&limit="+String((endEpoch/interval*interval-start)/interval)+"&end="+String(endEpoch/interval*interval-1)+"&exclude_current_candle=true";
  }
  CooperativeTlsClient tls;
  tls.setCACertBundle(caBundle); tls.setHandshakeTimeout(20); tls.setTimeout(6);
  HTTPClient http;
  http.setConnectTimeout(6000); http.setTimeout(6000);
  http.setReuse(false); http.useHTTP10(true); // Raw stream has no chunk framing.
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(tls,url)) return;
  http.setUserAgent("BitcoinClock/1.0");
  http.addHeader("Accept","application/json");
  http.addHeader("Accept-Encoding","identity"); http.addHeader("Cache-Control","no-cache");
  const char* headers[]={"Retry-After","Age","Date","Content-Type","Content-Encoding","Transfer-Encoding"};
  http.collectHeaders(headers,6);
  response.status=http.GET();
  response.epoch=uint32_t(time(nullptr));
  response.retryAfterMs=btc::retryAfter(http.header("Retry-After").c_str(),response.epoch);
  if (response.status==200 && http.getSize()<=int(history ? 160000 : 8192) &&
      http.header("Content-Type").startsWith("application/json") &&
      (http.header("Content-Encoding").isEmpty() || http.header("Content-Encoding")=="identity") &&
      http.header("Transfer-Encoding").isEmpty()) {
    String age=http.header("Age");
    uint32_t serverDate=btc::httpDate(http.header("Date").c_str());
    bool cached = !age.isEmpty() && age.toInt()>15;
    // All providers supply HTTP Date. This catches old cached responses even
    // when the ticker itself has no price timestamp.
    if (!cached && btc::freshTimestamp(serverDate,response.epoch,20)) {
      BoundedReader reader(*http.getStreamPtr(),history ? 160000 : 8192);
      if (document.capacity()) {
        DeserializationError error;
        StaticJsonDocument<768> filter;
        if(history)btc::historyFilter(filter,request.source==2);
        else btc::quoteFilter(filter,btc::kSources[request.source].provider);
        error=deserializeJson(document,reader,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(12));
        if (!error) {
          response.epoch=uint32_t(time(nullptr));
          response.ok=history ? btc::parseHistory(document.as<JsonVariantConst>(),request.source==2,request.window,endEpoch,response.history,historyStart)
                              : btc::parseQuote(btc::kSources[request.source].provider,document.as<JsonVariantConst>(),response.epoch,response.quote);
          if (!response.ok) { int code=apiError(document.as<JsonVariantConst>()); if (code) response.status=code; }
        } else Serial.printf("%s JSON failed: %s\n",btc::kSources[request.source].name,error.c_str());
      }
    }
  }
  http.end(); tls.stop();
}
void worker(void*) {
  market::Request request;
  for (;;) if (xQueueReceive(requests,&request,portMAX_DELAY)==pdTRUE) {
    fetch(request); response.receivedAt=millis();
    delay(2);
    const market::Result* reply=&response;
    xQueueSend(results,&reply,portMAX_DELAY);
    bool consumed=false;xQueueReceive(acknowledgments,&consumed,portMAX_DELAY);
#ifdef ARDUINO
    stackFree=uxTaskGetStackHighWaterMark(nullptr);
#endif
  }
}
}
namespace market {
bool begin() {
  requests=xQueueCreate(1,sizeof(Request));results=xQueueCreate(1,sizeof(const Result*));acknowledgments=xQueueCreate(1,sizeof(bool));
  auto cleanup=[](){if(requests)vQueueDelete(requests);if(results)vQueueDelete(results);if(acknowledgments)vQueueDelete(acknowledgments);requests=results=acknowledgments=nullptr;};
  if (!requests || !results || !acknowledgments) {cleanup();return false;}
  // TLS certificate/key calculations can run continuously for several seconds.
  // Share the idle task's priority so those calculations cannot starve IDLE0
  // or restart the device while the independent stream/display keep running.
  // Device measurements left >6 KiB of the old 16 KiB stack unused even for
  // manifest verification. Keep a 4 KiB margin with a 14 KiB worker.
  if(xTaskCreatePinnedToCore(worker,"btc-http",14336,nullptr,0,nullptr,0)!=pdPASS){cleanup();return false;}
  return true;
}
bool send(const Request& request) { return requests && xQueueSend(requests,&request,0)==pdTRUE; }
bool receive(ResultLease& result){return !result.value_&&results&&xQueueReceive(results,&result.value_,0)==pdTRUE;}
ResultLease::~ResultLease(){if(value_&&acknowledgments){bool consumed=true;xQueueSend(acknowledgments,&consumed,0);}}
uint32_t stackRemaining(){return stackFree.load();}
}
