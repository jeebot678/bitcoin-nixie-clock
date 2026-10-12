#include <cassert>
#include <fstream>
#include <sstream>
#include <iostream>
#include <time.h>
#include "Arduino.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "MarketClient.h"
#include "OtaUpdate.h"
#include "TlsGate.h"

struct Queue{size_t capacity,size;std::vector<std::vector<uint8_t>> messages;Queue(size_t n,size_t s):capacity(n),size(s){}};
using QueueHandle_t=Queue*;
constexpr int pdTRUE=1,pdPASS=1,portMAX_DELAY=0;
inline QueueHandle_t xQueueCreate(int n,size_t size){return new Queue(n,size);}
inline void vQueueDelete(QueueHandle_t q){delete q;}
inline int xQueueReceive(QueueHandle_t q,void* data,int){if(q->messages.empty())return 0;memcpy(data,q->messages.front().data(),q->size);q->messages.erase(q->messages.begin());return pdTRUE;}
inline int xQueueSend(QueueHandle_t q,const void* data,int){if(q->messages.size()==q->capacity)return 0;const auto* bytes=static_cast<const uint8_t*>(data);q->messages.emplace_back(bytes,bytes+q->size);return pdTRUE;}
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,int stack,void*,int priority,void*,int core){assert(stack==14336&&priority==0&&core==0);return pdPASS;}
SerialMock Serial;EspMock ESP;WifiMock WiFi;uint32_t testMillis=100;
std::map<int,int> pins,modes,adc;std::vector<GpioEvent> gpioEvents;
const uint8_t caBundle[] asm("_binary_data_cert_x509_crt_bundle_bin_start")={0};
time_t testEpoch;
time_t testTime(time_t* out){if(out)*out=testEpoch;return testEpoch;}
unsigned otaCalls=0,pauseCalls=0,resumeCalls=0;bool streamPaused=true;
namespace ota {bool checkAndInstall(){++otaCalls;return true;}}
namespace live {bool prepareHttp(size_t capacity,uint32_t timeout,bool probe){assert(capacity>0&&timeout==30000);if(probe)assert(bool(btc::tlsHandshakeGate().load()));++pauseCalls;return streamPaused;}}
#define time testTime
#include "../../src/MarketClient.cpp"
#undef time

std::string readFile(const std::string& path){std::ifstream f(path);assert(f.good());std::ostringstream s;s<<f.rdbuf();return s.str();}
HttpResponse fixture(const std::string& path){HttpResponse reply;std::string body=readFile(path);reply.body.assign(body.begin(),body.end());reply.headers["Content-Type"]="application/json";
  char date[64];strftime(date,sizeof(date),"%a, %d %b %Y %H:%M:%S GMT",gmtime(&testEpoch));reply.headers["Date"]=date;return reply;
}
int main(int argc,char** argv){assert(argc==4);std::string directory=argv[1];testEpoch=time_t(strtoul(argv[2],nullptr,10));WiFi.connection=WL_CONNECTED;
  const char* names[]={"coinbase","kraken","bitstamp","gemini","bitfinex","binance","okx","kucoin","gate","bitget","mexc"};
  for(uint8_t source=0;source<11;++source){httpResponses()[btc::kSources[source].url]=fixture(directory+"/"+names[source]+".json");fetch({market::Kind::Quote,source,0,uint32_t(testEpoch)});assert(response.ok&&response.quote.price>80000);}
  testEpoch=time_t(strtoul(argv[3],nullptr,10));
  const char* windows[]={"5m","30m","hour","day","week"};
  for(uint8_t source=1;source<=2;++source)for(uint8_t window=0;window<5;++window){uint32_t interval=config::kCandleSeconds[window];
    uint32_t start=btc::historyRequestStart(window,uint32_t(testEpoch));
    std::string url=source==1?"https://api.kraken.com/0/public/OHLC?pair=XBTUSD&interval="+std::to_string(interval/60)+"&since="+std::to_string(start-interval):"https://www.bitstamp.net/api/v2/ohlc/btcusd/?step="+std::to_string(interval)+"&limit="+std::to_string((testEpoch/interval*interval-start)/interval)+"&end="+std::to_string(testEpoch/interval*interval-1)+"&exclude_current_candle=true";
    httpResponses()[url]=fixture(directory+"/"+(source==1?"kraken_":"bitstamp_")+windows[window]+".json");fetch({market::Kind::History,source,window,uint32_t(testEpoch)});assert(response.ok&&response.history.count>2);
    auto reply=httpResponses()[url];reply.body.resize(reply.body.size()/2);httpResponses()[url]=reply;fetch({market::Kind::History,source,window,uint32_t(testEpoch)});assert(!response.ok);
  }
  // One-candle and 48-hour delta requests use a bounded pool and the exact
  // missing suffix. Old rows returned by Kraken cannot overwrite earlier data.
  for(uint8_t source=1;source<=2;++source)for(unsigned rows:{1U,48U}){
    uint8_t window=4;uint32_t step=3600,last=uint32_t(testEpoch)/step*step,start=last-rows*step;
    std::string url=source==1?"https://api.kraken.com/0/public/OHLC?pair=XBTUSD&interval=60&since="+std::to_string(start-step):"https://www.bitstamp.net/api/v2/ohlc/btcusd/?step=3600&limit="+std::to_string(rows)+"&end="+std::to_string(last-1)+"&exclude_current_candle=true";
    httpResponses()[url]=fixture(directory+"/"+(source==1?"kraken_":"bitstamp_")+"week_delta_"+std::to_string(rows)+".json");
    fetch({market::Kind::History,source,window,uint32_t(testEpoch),start});assert(response.ok&&response.history.count==rows);
    assert(response.history.samples[0].timestamp==start+step&&response.history.samples[rows-1].timestamp==last);
    assert(btc::historyCapacity(source==2,window,uint32_t(testEpoch),start)<btc::historyCapacity(source==2,window));
  }
  testEpoch=time_t(strtoul(argv[2],nullptr,10));
  auto request=market::Request{market::Kind::Quote,0,0,uint32_t(testEpoch)};
  auto good=httpResponses()[btc::kSources[0].url];auto reply=good;reply.headers["Age"]="100";httpResponses()[btc::kSources[0].url]=reply;fetch(request);assert(!response.ok);
  reply=good;reply.headers["Date"]="Wed, 01 Jan 2025 00:00:00 GMT";httpResponses()[btc::kSources[0].url]=reply;fetch(request);assert(!response.ok);
  reply=good;reply.headers["Transfer-Encoding"]="chunked";httpResponses()[btc::kSources[0].url]=reply;fetch(request);assert(!response.ok);
  reply=good;reply.declaredSize=8193;httpResponses()[btc::kSources[0].url]=reply;fetch(request);assert(!response.ok);
  reply=good;reply.status=429;reply.headers["Retry-After"]="90";httpResponses()[btc::kSources[0].url]=reply;fetch(request);assert(!response.ok&&response.status==429&&response.retryAfterMs==90000);
  // Manifest checks reserve HTTP memory without blindly pausing the primary.
  unsigned reservations=pauseCalls;
  fetch({market::Kind::Ota,0,0,uint32_t(testEpoch)});assert(response.ok&&otaCalls==1&&pauseCalls==reservations+2&&!resumeCalls);
  streamPaused=false;fetch({market::Kind::Ota,0,0,uint32_t(testEpoch)});assert(!response.ok&&otaCalls==1&&pauseCalls==reservations+3);streamPaused=true;
  {btc::TlsLease streamHandshake;assert(streamHandshake.acquire());uint32_t waitingAt=testMillis;fetch(request);assert(!response.ok&&uint32_t(testMillis-waitingAt)>=15000);}
  httpResponses()[btc::kSources[0].url]=good;fetch(request);assert(response.ok); // Gate releases on every return path.
  // The result queue stores one pointer; the worker's buffer cannot be reused
  // until the main loop returns its unique response lease (including early returns).
  assert(market::begin()&&results->size==sizeof(const market::Result*));
  const market::Result* pointer=&response;assert(xQueueSend(results,&pointer,0)==pdTRUE);
  {market::ResultLease lease;assert(market::receive(lease)&&&lease.get()==&response);
    assert(!market::receive(lease)&&acknowledgments->messages.empty());}
  bool consumed=false;assert(xQueueReceive(acknowledgments,&consumed,0)==pdTRUE&&consumed);
  {market::ResultLease empty;assert(!market::receive(empty));}assert(acknowledgments->messages.empty());
  vQueueDelete(requests);vQueueDelete(results);vQueueDelete(acknowledgments);requests=results=acknowledgments=nullptr;
  WiFi.connection=0;fetch(request);assert(!response.ok);
  std::cout<<"PASS: actual HTTP worker; all 11 quote and 10 history fixtures, truncation, cache freshness, framing, size bounds, rate limits and Wi-Fi loss\n";
}
