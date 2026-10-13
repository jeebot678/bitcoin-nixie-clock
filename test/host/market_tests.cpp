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
unsigned otaCalls=0,pauseCalls=0,resumeCalls=0;bool streamPaused=true;size_t lastReservationBytes=0;
namespace ota {bool checkAndInstall(){++otaCalls;return true;}}
namespace live {bool prepareHttp(size_t capacity,uint32_t timeout,bool probe,bool allowPrimaryPause){assert(capacity>0&&timeout==30000);if(probe)assert(bool(btc::tlsHandshakeGate().load()));(void)allowPrimaryPause;lastReservationBytes=capacity;++pauseCalls;return streamPaused;}}
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
  const char* windows[]={"unused","unused","5m","30m","hour"};
  for(uint8_t source=1;source<=2;++source)for(uint8_t window=2;window<5;++window){uint32_t interval=config::kCandleSeconds[window];
    uint32_t start=btc::historyRequestStart(window,uint32_t(testEpoch));uint32_t end=source==2?std::min<uint32_t>(testEpoch,start+24*interval):testEpoch;
    std::string url=source==1?"https://api.kraken.com/0/public/OHLC?pair=XBTUSD&interval="+std::to_string(interval/60)+"&since="+std::to_string(start-interval):"https://www.bitstamp.net/api/v2/ohlc/btcusd/?step="+std::to_string(interval)+"&limit="+std::to_string((end/interval*interval-start)/interval)+"&end="+std::to_string(end/interval*interval-1)+"&exclude_current_candle=true";
    httpResponses()[url]=fixture(directory+"/"+(source==1?"kraken_":"bitstamp_")+windows[window]+".json");
    if(source==2){DynamicJsonDocument page(100000);assert(!deserializeJson(page,(const char*)httpResponses()[url].body.data(),httpResponses()[url].body.size()));auto rows=page["data"]["ohlc"].as<JsonArray>();while(rows.size()&&btc::numericTimestamp(rows[rows.size()-1]["timestamp"])+interval>end)rows.remove(rows.size()-1);std::string body;serializeJson(page,body);httpResponses()[url].body.assign(body.begin(),body.end());}fetch({market::Kind::History,source,window,uint32_t(testEpoch)});assert(response.ok&&response.history.count>2);
    auto reply=httpResponses()[url];reply.body.resize(reply.body.size()/2);httpResponses()[url]=reply;fetch({market::Kind::History,source,window,uint32_t(testEpoch)});assert(!response.ok);
  }
  // Recent transactions stream into one-second caches with a 512-byte pool.
  StaticJsonDocument<4096> recentReport;assert(!deserializeJson(recentReport,readFile("test/recent_trades_probe_report.json")));
  for(uint8_t source:{uint8_t(0),uint8_t(2)})for(uint8_t window=0;window<2;++window){
    const char* name=source==2?"bitstamp":"coinbase";testEpoch=recentReport["endpoints"][name]["epoch"].as<uint32_t>();
    std::string url=source==2?"https://www.bitstamp.net/api/v2/transactions/btcusd/?time=minute":"https://api.exchange.coinbase.com/products/BTC-USD/trades?limit=100";
    httpResponses()[url]=fixture(directory+"/"+name+"_recent_trades.json");fetch({market::Kind::History,source,window,uint32_t(testEpoch)});
    assert(response.ok&&response.history.count>2&&response.history.count<=61);
    for(unsigned i=0;i<response.history.count;++i)assert(response.history.samples[i].timestamp>=testEpoch-config::kWindowSeconds[window]&&response.history.samples[i].timestamp<=testEpoch);
    auto bad=httpResponses()[url];bad.body.pop_back();httpResponses()[url]=bad;fetch({market::Kind::History,source,window,uint32_t(testEpoch)});assert(!response.ok);
  }
  // Actual candle HTTP routes stream minute/hour transactions and Coinbase
  // 1000-trade pages, with real high/low values and a 512-byte object pool.
  DynamicJsonDocument candleReport(100000);assert(!deserializeJson(candleReport,readFile("test/candle_trades_probe_report.json")));
  for(uint8_t source:{uint8_t(0),uint8_t(2)})for(uint8_t window=0;window<5;++window){
    const char* name=source==2?"bitstamp":"coinbase";testEpoch=candleReport[name]["epoch"].as<uint32_t>();
    std::string url=source==2?std::string("https://www.bitstamp.net/api/v2/transactions/btcusd/?time=")+(window<2?"minute":"hour"):"https://api.exchange.coinbase.com/products/BTC-USD/trades?limit=1000";
    httpResponses()[url]=fixture(directory+"/"+name+"_candle_trades.json");fetch({market::Kind::Candles,source,window,uint32_t(testEpoch)});
    assert(response.ok&&response.candles[window].count(window,uint64_t(testEpoch)*1000)&&response.quote.price>0);
    uint8_t attempted=source==2&&window<2?0x03:0x1F;assert(response.candleAttemptMask==attempted);
    assert(response.candleSnapshotMs==uint64_t(testEpoch)*1000&&response.candleOldestMs&&response.candleOldestMs<=response.candleNewestMs&&response.candleNewestMs<=response.candleSnapshotMs);
    assert(lastReservationBytes==512+sizeof(btc::TradeCandleScratch)&&testAllocations().empty());
    for(uint8_t range=0;range<5;++range){
      auto expected=candleReport[name]["expected_candles"][std::to_string(range)].as<JsonObjectConst>();
      if(!(attempted&(1U<<range))){assert(!response.candles[range].count(range,uint64_t(testEpoch)*1000));continue;}
      assert(response.candles[range].count(range,uint64_t(testEpoch)*1000)==expected.size());
      for(auto entry:expected){uint32_t id=uint32_t(strtoul(entry.key().c_str(),nullptr,10));const auto& bar=response.candles[range].bars[id%21];auto values=entry.value().as<JsonArrayConst>();
        assert(bar.bucket==id&&bar.open==values[0].as<uint32_t>()&&bar.high==values[1].as<uint32_t>()&&bar.low==values[2].as<uint32_t>()&&bar.close==values[3].as<uint32_t>());}
      bool covered=source==2&&response.candleOldestMs<=btc::candleStart(btc::candleBucket(response.candleSnapshotMs,range)-20,range);
      assert(bool(response.candleCoverageMask&(1U<<range))==covered);
    }
    if(source==0)assert(!response.candleCoverageMask); // Latest 1000 trades remain partial.
    auto truncated=httpResponses()[url];truncated.body.resize(truncated.body.size()-2);httpResponses()[url]=truncated;fetch({market::Kind::Candles,source,window,uint32_t(testEpoch)});assert(!response.ok);
    assert(!response.candleCoverageMask&&!response.candleSnapshotMs&&testAllocations().empty());
  }
  // Empty/expired pages and malformed bodies cannot suppress future bootstrap.
  // A successful sparse Bitstamp page is useful, but its provider name alone
  // does not prove that it reaches the beginning of any requested range.
  for(uint8_t source:{uint8_t(0),uint8_t(2)})for(uint8_t window=0;window<5;++window){
    std::string url=source==2?std::string("https://www.bitstamp.net/api/v2/transactions/btcusd/?time=")+(window<2?"minute":"hour"):"https://api.exchange.coinbase.com/products/BTC-USD/trades?limit=1000";
    HttpResponse page=fixture(directory+(source==2?"/bitstamp_candle_trades.json":"/coinbase_candle_trades.json"));
    std::string expired=source==2?std::string("[{\"date\":\"")+std::to_string(testEpoch-7200)+"\",\"tid\":\"1\",\"price\":\"80000\"}]":std::string("[{\"time\":\"2026-01-01T00:00:00Z\",\"trade_id\":1,\"price\":\"80000\"}]");
    for(const std::string& body:{std::string("[]"),expired}){
      page.body.assign(body.begin(),body.end());httpResponses()[url]=page;fetch({market::Kind::Candles,source,window,uint32_t(testEpoch)});
      assert(!response.ok&&!response.candleCoverageMask&&testAllocations().empty());
    }
    if(source==2){
      std::string body="[{\"date\":\""+std::to_string(testEpoch)+"\",\"tid\":\"3\",\"price\":\"80003\"},{\"date\":\""+std::to_string(testEpoch)+"\",\"tid\":\"1\",\"price\":\"80001\"},{\"date\":\""+std::to_string(testEpoch)+"\",\"tid\":\"2\",\"price\":\"79999\"}]";
      page.body.assign(body.begin(),body.end());httpResponses()[url]=page;fetch({market::Kind::Candles,source,window,uint32_t(testEpoch)});
      assert(response.ok&&!response.candleCoverageMask&&testAllocations().empty());
      for(uint8_t range=0;range<5;++range)if(response.candleAttemptMask&(1U<<range)){
        assert(response.candles[range].count(range,response.candleSnapshotMs)==1);
        const auto& bar=response.candles[range].bars[btc::candleBucket(response.candleSnapshotMs,range)%21];
        assert(bar.open==8000100&&bar.close==8000300&&bar.high==8000300&&bar.low==7999900);
      }
      // Allocation failure is a local deferral, and cannot leak scratch RAM or
      // falsely count as complete history. No worker stack increase is needed.
      testDataHeap()=sizeof(btc::TradeCandleScratch)-1;fetch({market::Kind::Candles,source,window,uint32_t(testEpoch)});
      assert(!response.ok&&response.deferred&&!response.candleCoverageMask&&testAllocations().empty());testDataHeap()=UINT32_MAX;
    }
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
  // Cache IO shares the existing worker without touching TLS/provider state.
  // The queued copy remains immutable while the display app changes its candle.
  btc::CandleSeries cacheSeries[5];btc::CandleScale cacheScales[5];CandleStore store;assert(store.begin(cacheSeries,cacheScales));
  uint64_t stamp=uint64_t(testEpoch)*1000;cacheSeries[0].add(stamp,83300,0,stamp);cacheScales[0].update(cacheSeries[0],0,stamp);
  auto before=cacheSeries[0];unsigned reservationsBefore=pauseCalls;
  assert(market::sendCache(store,0,cacheSeries[0],cacheScales[0],stamp));
  assert(!market::sendCache(store,0,cacheSeries[0],cacheScales[0],stamp));
  market::Request cacheRequest;assert(xQueueReceive(requests,&cacheRequest,0)==pdTRUE&&cacheRequest.kind==market::Kind::SaveCache);
  cacheSeries[0].add(stamp,83400,0,stamp);WiFi.connection=0;fetch(cacheRequest);
  assert(response.ok&&response.cacheEpochMs==stamp&&pauseCalls==reservationsBefore);
  btc::CandleSeries restored[5];btc::CandleScale restoredScales[5];CandleStore boot;assert(boot.begin(restored,restoredScales));assert(!memcmp(&before,&restored[0],sizeof(before)));
  pointer=&response;assert(xQueueSend(results,&pointer,0)==pdTRUE);{market::ResultLease lease;assert(market::receive(lease));}
  assert(xQueueReceive(acknowledgments,&consumed,0)==pdTRUE);assert(!cachePending.load());
  WiFi.connection=WL_CONNECTED;
  vQueueDelete(requests);vQueueDelete(results);vQueueDelete(acknowledgments);requests=results=acknowledgments=nullptr;
  WiFi.connection=0;fetch(request);assert(!response.ok);
  std::cout<<"PASS: actual HTTP worker; all 11 quote and 10 history fixtures, shared five-range OHLC bootstrap, partial/empty/expired coverage, heap scratch reservation/release, truncation, cache freshness, framing, size bounds, rate limits and Wi-Fi loss\n";
}
