#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "MarketClient.h"
#include "OtaUpdate.h"

extern const uint8_t caBundle[] asm("_binary_data_cert_x509_crt_bundle_bin_start");
namespace {
QueueHandle_t requests = nullptr, results = nullptr;
// The worker owns this buffer. Queue copies keep main/worker ownership separate.
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
  response.ok=false; response.status=0; response.retryAfterMs=0;
  response.quote={}; response.history.count=0; response.request=request;
  if (WiFi.status()!=WL_CONNECTED || time(nullptr)<config::kMinimumEpoch) return;
  if (request.kind==market::Kind::Ota) { response.ok=ota::checkAndInstall();return; }
  String url;
  bool history=request.kind==market::Kind::History;
  if (!history) url=btc::kSources[request.source].url;
  else {
    uint32_t interval=config::kCandleSeconds[request.window];
    if (request.source==1)
      url="https://api.kraken.com/0/public/OHLC?pair=XBTUSD&interval="+String(interval/60)+"&since="+String(request.epoch-config::kWindowSeconds[request.window]-interval*2);
    else
      url="https://www.bitstamp.net/api/v2/ohlc/btcusd/?step="+String(interval)+"&limit="+String(config::kWindowSeconds[request.window]/interval+3);
  }
  WiFiClientSecure tls;
  tls.setCACertBundle(caBundle); tls.setHandshakeTimeout(6); tls.setTimeout(6);
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
      DynamicJsonDocument document(history ? 81920 : 8192);
      if (document.capacity()) {
        DeserializationError error;
        if (history) {
          StaticJsonDocument<512> filter;
          btc::historyFilter(filter,request.source==2);
          error=deserializeJson(document,reader,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(12));
        } else error=deserializeJson(document,reader,DeserializationOption::NestingLimit(12));
        if (!error) {
          response.epoch=uint32_t(time(nullptr));
          response.ok=history ? btc::parseHistory(document.as<JsonVariantConst>(),request.source==2,request.window,response.epoch,response.history)
                              : btc::parseQuote(btc::kSources[request.source].provider,document.as<JsonVariantConst>(),response.epoch,response.quote);
          if (!response.ok) { int code=apiError(document.as<JsonVariantConst>()); if (code) response.status=code; }
        }
      }
    }
  }
  http.end(); tls.stop();
}
void worker(void*) {
  market::Request request;
  for (;;) if (xQueueReceive(requests,&request,portMAX_DELAY)==pdTRUE) {
    fetch(request); response.receivedAt=millis();
    xQueueSend(results,&response,portMAX_DELAY);
  }
}
}
namespace market {
bool begin() {
  requests=xQueueCreate(1,sizeof(Request)); results=xQueueCreate(1,sizeof(Result));
  if (!requests || !results) return false;
  return xTaskCreatePinnedToCore(worker,"btc-http",12288,nullptr,1,nullptr,0)==pdPASS;
}
bool send(const Request& request) { return requests && xQueueSend(requests,&request,0)==pdTRUE; }
bool receive(Result& result) { return results && xQueueReceive(results,&result,0)==pdTRUE; }
}
