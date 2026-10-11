#include <cassert>
#include <fstream>
#include <sstream>
#include <iostream>
#include <limits>
#include <set>
#include "MarketParsers.h"
#include "WifiCredentials.h"
#include "OtaManifest.h"
#include "LedMatrixMap.h"
#include "TlsGate.h"
#include "QuoteBook.h"

std::string readFile(const std::string& path) { std::ifstream file(path); assert(file.good()); std::ostringstream out;out<<file.rdbuf();return out.str(); }
size_t slotsIn(JsonVariantConst value) {
  size_t slots=0;
  if (value.is<JsonArrayConst>()) for (JsonVariantConst child:value.as<JsonArrayConst>()) slots+=1+slotsIn(child);
  else if (value.is<JsonObjectConst>()) for (JsonPairConst child:value.as<JsonObjectConst>()) slots+=1+slotsIn(child.value());
  return slots;
}
void coreTests() {
  uint8_t digits[6];assert(btc::priceDigits(123456.49,digits));assert(digits[0]==1&&digits[5]==6);
  assert(btc::priceDigits(999999.49,digits));assert(!btc::priceDigits(999999.5,digits));assert(!btc::priceDigits(1000000,digits));
  assert(btc::priceDigits(83271.50,digits));assert(digits[0]==0&&digits[5]==2);
  for (double bad:{0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) assert(!btc::priceDigits(bad,digits));
  for (int i=0;i<5;++i) { assert(btc::dialPosition(config::kRefreshMv[i],config::kRefreshMv)==i); assert(btc::dialPosition(config::kRefreshMv[i]+150,config::kRefreshMv)==i); }
  assert(btc::dialPosition(3200,config::kRefreshMv)==-1);assert(btc::dialPosition(2269,config::kRefreshMv)==-1);
  for(uint32_t measured:{142U,155U,177U,182U,216U,222U,237U,240U,301U,309U,342U})
    assert(btc::dialPosition(measured,config::kRefreshMv)==0);
  assert(btc::dialPosition(343,config::kRefreshMv)==1);
  btc::Dial dial; assert(!dial.update(0,100));assert(!dial.update(0,249));assert(dial.update(0,250));
  assert(!dial.update(4,251));assert(!dial.update(-1,300));assert(!dial.update(4,400));assert(dial.update(4,550));assert(dial.stable==4);
  dial={};assert(!dial.update(2,UINT32_MAX-50));assert(dial.update(2,100));
  assert(btc::due(30,UINT32_MAX-100));assert(!btc::due(UINT32_MAX-100,30));
  btc::Rotation rotation;std::set<int> picked;
  for (size_t i=0;i<btc::kSourceCount;++i) { int source=rotation.choose(1000,true);assert(source>=0);picked.insert(source);rotation.started(source,1000); }
  assert(picked.size()==11);assert(rotation.choose(1000,true)==-1);assert(rotation.choose(6000,true)>=0);
  rotation.failure(0,7000,429,120000);assert(!btc::due(126999,rotation.states[0].availableAt));assert(btc::due(127000,rotation.states[0].availableAt));
  rotation.failure(1,8000,403);assert(rotation.states[1].availableAt==3608000);
  rotation.failure(2,UINT32_MAX-100,500);assert(!btc::due(100,rotation.states[2].availableAt));assert(btc::due(5000,rotation.states[2].availableAt));
  for(int i=0;i<100;++i) {int source=rotation.choose(10000000,false);assert(source>=0&&!btc::kSources[source].usdt);}
  btc::FxRate fx;double normalized;assert(!fx.normalize(100,true,0,normalized));fx.usd=.998;fx.receivedAt=100;
  assert(fx.normalize(100000,true,200,normalized)&&normalized==99800);assert(!fx.normalize(100000,true,120101,normalized));assert(fx.normalize(100000,false,120101,normalized));
  btc::PriceGuard guard;assert(guard.accept(80000,0,0)&&guard.price==80000&&!guard.confirmed());
  assert(guard.accept(80001,0,10)&&!guard.confirmed());assert(guard.accept(80002,1,20)&&guard.confirmed());
  assert(!guard.accept(160000,2,100));assert(guard.accept(80003,3,200));assert(!guard.accept(100000,2,300));assert(guard.accept(100001,3,400));
  assert(!guard.accept(0,1,500));
  btc::PriceGuard initial;assert(!initial.accept(NAN,0,0)&&!initial.price);
  assert(initial.accept(80000,0,UINT32_MAX-100)&&!initial.confirmed());
  assert(!initial.accept(100000,1,0)&&initial.price==80000&&!initial.confirmed());
  assert(!initial.accept(100001,1,10)); // One disagreeing provider cannot confirm itself.
  assert(initial.accept(100002,2,20)&&initial.confirmed()&&initial.price==100002);
  btc::QuoteBook book;btc::DisplayQuote selected;
  book.put(80000.1,0,100,1800000000);book.put(80007.2,1,100,1800000001);
  for(unsigned i=0;i<100;++i)book.put(80007.2,1,101+i,1800000001);
  assert(book.choose(250,80007.2,1,80007.2,false,selected)&&selected.source==0&&selected.price==80000.1&&selected.receivedAt==100);
  assert(book.choose(500,80000.1,0,80007.2,false,selected)&&selected.source==1);
  assert(book.choose(5100,80007.2,1,80007.2,false,selected)&&selected.source==0);
  assert(!book.choose(5201,80000.1,0,80007.2,false,selected)); // Never recycle expired quotes.
  book.clear();book.put(80000.1,0,100,1);book.put(80000.2,1,100,1);
  assert(book.choose(101,80000.1,0,80000,false,selected)&&selected.source==1&&selected.price==80000.2); // No invented whole dollars.
  assert(!book.choose(101,80000.1,0,160000,false,selected)); // Old cache cannot resurrect a confirmed large move.
  book.clear();book.put(80001,5,100,1);assert(!book.choose(101,80000,0,80000,false,selected));
  assert(book.choose(101,80000,0,80000,true,selected)); // FX expiry excludes USDT caches.
  book.clear();book.put(80003,0,UINT32_MAX-100,1);assert(book.choose(200,80000,1,80000,false,selected));
  book.put(NAN,1,200,1);book.put(1,255,200,1);assert(selected.price==80003);
  bool cosmetic=false;assert(btc::displayDollars(80000.1,80000,true,cosmetic)==80001&&cosmetic);
  assert(btc::displayDollars(80000.1,80001,true,cosmetic)==80000&&!cosmetic);
  assert(btc::displayDollars(80000,80000,true,cosmetic)==80001&&cosmetic);
  assert(btc::displayDollars(80000.9,80001,true,cosmetic)==80000&&cosmetic);
  assert(btc::displayDollars(80000.1,80000,false,cosmetic)==80000&&!cosmetic);
  assert(btc::displayDollars(999999.1,999999,true,cosmetic)==999999&&!cosmetic);
  for(unsigned cents=0;cents<100;++cents){double raw=80000+cents/100.0;uint32_t last=80000;
    for(unsigned frame=0;frame<100;++frame){uint32_t next=btc::displayDollars(raw,last,true,cosmetic);assert(next!=last&&std::abs(double(next)-raw)<=1);last=next;}}
  btc::History history;uint32_t now=1800000000;
  for(unsigned i=0;i<21;++i) assert(history.add(now-3600+i*170,80000+i*100));
  auto plot=btc::makePlot(history,now,3600,82200,now);assert(plot.valid);for(int i=0;i<21;++i)if(plot.valid&(1U<<i))assert(plot.rows[i]<=12);
  btc::History empty;auto sparse=btc::makePlot(empty,now,604800,80000,now);assert(sparse.valid==(1U<<20));assert(!btc::makePlot(empty,now,604800,80000,now-604801).valid);
  auto unchanged=btc::makePlot(empty,now,604800,80000.01,now);assert(unchanged.valid==sparse.valid&&unchanged.rows[20]==sparse.rows[20]);
  // Every selected range divides into exactly 21 chronological buckets, with
  // the latest sample in each bucket determining its one price pixel.
  for (uint32_t window:config::kWindowSeconds) {
    uint32_t start=now-window;
    for (unsigned column=0;column<21;++column) {
      uint32_t first=(uint64_t(column)*window+20)/21;
      uint32_t last=column==20?window:(uint64_t(column+1)*window+20)/21-1;
      for (uint32_t offset:{first,last}) {
        auto cell=btc::makePlot(empty,now,window,80000,start+offset);
        assert(cell.valid==(1U<<column)&&cell.rows[column]==6);
      }
    }
    assert(!btc::makePlot(empty,now,window,80000,start-1).valid);
    assert(!btc::makePlot(empty,now,window,80000,now+1).valid);
    btc::History increasing;
    for (unsigned column=0;column<21;++column) {
      uint32_t timestamp=start+(uint64_t(column)*window+20)/21;
      assert(increasing.add(timestamp,80000+column*100));
    }
    auto full=btc::makePlot(increasing,now,window,0,0);
    assert(full.valid==((1U<<21)-1));
    for (unsigned column=0;column<21;++column) {
      assert(full.rows[column]<=12);
      if (column) assert(full.rows[column]<=full.rows[column-1]);
    }
    auto latest=btc::makePlot(increasing,now,window,70000,now);
    assert(latest.rows[20]>full.rows[20]);
    btc::History completed;
    assert(completed.add(now,82000));
    auto current=btc::makePlot(completed,now,window,0,0);
    auto older=btc::makePlot(completed,now,window,70000,now-1);
    assert(older.valid==current.valid&&older.rows[20]==current.rows[20]);
  }
  {btc::TlsLease owner,contender;assert(owner.acquire()&&owner.acquire()&&!contender.acquire());}
  {btc::TlsLease next;assert(next.acquire());}
  assert(!history.add(now-4000,10));assert(!history.add(now,0));
  assert(btc::validCredentials("home","password123"));assert(btc::validCredentials("open",""));assert(!btc::validCredentials("","password123"));assert(!btc::validCredentials("home","short"));
  std::string hex(64,'a');assert(btc::validCredentials("home",hex.c_str()));hex[2]='g';assert(!btc::validCredentials("home",hex.c_str()));
  btc::WifiCredentials stored;strcpy(stored.ssid,"home");strcpy(stored.password,"password123");assert(btc::validStoredCredentials(stored));stored.version=2;assert(!btc::validStoredCredentials(stored));stored.version=1;memset(stored.ssid,'x',33);assert(!btc::validStoredCredentials(stored));
  assert(btc::isoTimestamp("2026-10-07T21:32:30.145104442Z")==1791408750);assert(!btc::isoTimestamp("2026-02-30T00:00:00Z"));assert(!btc::isoTimestamp("2026-10-07T21:32:30+00:00"));
  assert(btc::httpDate("Wed, 07 Oct 2026 21:32:30 GMT")==1791408750);assert(btc::retryAfter("120",now)==120000);assert(btc::retryAfter("junk",now)==0);
  std::set<unsigned> mapped;for(uint8_t row=0;row<13;++row)for(uint8_t col=0;col<21;++col){auto p=led_matrix::mapPixel(row,col);assert(p.valid&&p.driver<6&&p.digit<8&&p.segmentBit<8);assert(mapped.insert(p.driver*64+p.digit*8+p.segmentBit).second);}
  assert(mapped.size()==273&&!led_matrix::mapPixel(13,0).valid&&!led_matrix::mapPixel(0,21).valid);
}
void fixtures(const std::string& directory,uint32_t now,uint32_t historyEpoch) {
  const char* names[]={"coinbase","kraken","bitstamp","gemini","bitfinex","binance","okx","kucoin","gate","bitget","mexc"};
  for(size_t i=0;i<11;++i){DynamicJsonDocument doc(16000);assert(!deserializeJson(doc,readFile(directory+"/"+names[i]+".json")));btc::Quote quote;
    assert(btc::parseQuote(btc::kSources[i].provider,doc.as<JsonVariantConst>(),now,quote));assert(quote.price>80000&&quote.price<90000);
    if(quote.timestamp) assert(!btc::parseQuote(btc::kSources[i].provider,doc.as<JsonVariantConst>(),now+1000,quote));
    if(i==1)assert(quote.usdtUsd>.99&&quote.usdtUsd<1.01);
    assert(!btc::parseQuote(btc::kSources[i].provider,JsonVariantConst(),now,quote));
  }
  now=historyEpoch;
  const char* windows[]={"5m","30m","hour","day","week"};
  for(int provider=0;provider<2;++provider)for(uint8_t window=0;window<5;++window){
    DynamicJsonDocument doc(btc::historyCapacity(provider==1,window));StaticJsonDocument<1024> filter;btc::historyFilter(filter,provider==1);
    assert(!deserializeJson(doc,readFile(directory+"/"+(provider?"bitstamp_":"kraken_")+windows[window]+".json"),DeserializationOption::Filter(filter)));
    btc::History history;assert(btc::parseHistory(doc.as<JsonVariantConst>(),provider==1,window,now,history));
    assert(history.count>2&&history.count<=btc::kMaxCandles);for(size_t i=0;i<history.count;++i)assert(history.samples[i].timestamp<=now);
    auto plot=btc::makePlot(history,now,config::kWindowSeconds[window],83277.36,now);assert(__builtin_popcount(plot.valid)>=int(std::min<size_t>(18,history.count)));
    // Estimated 32-bit ESP32 allocation: slots halve; strings remain unchanged.
    size_t hostUsage=doc.memoryUsage();size_t slots=slotsIn(doc.as<JsonVariantConst>());
    size_t espUsage=hostUsage-slots*(JSON_ARRAY_SIZE(1)-16);
    size_t espBudget=2048+(config::kWindowSeconds[window]/config::kCandleSeconds[window]+4)*(provider?96:208);
    assert(espUsage<espBudget);
    std::cout<<(provider?"Bitstamp ":"Kraken ")<<windows[window]<<": "<<history.count<<" closed candles; ESP32 parser "<<espUsage<<" bytes\n";
  }
  DynamicJsonDocument bad(2048);btc::Quote quote;
  for(const char* payload:{"{\"symbol\":\"ETHUSDT\",\"price\":\"80000\"}","{\"symbol\":\"BTCUSDT\",\"price\":\"NaN\"}","{\"symbol\":\"BTCUSDT\",\"price\":\"123abc\"}","{\"symbol\":\"BTCUSDT\",\"price\":true}"}){assert(!deserializeJson(bad,payload));assert(!btc::parseQuote(btc::Provider::Binance,bad.as<JsonVariantConst>(),now,quote));}
  assert(deserializeJson(bad,"{invalid"));
  assert(!deserializeJson(bad,"{\"code\":\"50011\",\"data\":[]}"));assert(!btc::parseQuote(btc::Provider::Okx,bad.as<JsonVariantConst>(),now,quote));
}
void otaTests(){
  assert(ota::newer("1.0.1","1.0.0"));assert(ota::newer("1.10.0","1.9.9"));assert(!ota::newer("1.0.0","1.0.0"));assert(!ota::newer("0.9.9","1.0.0"));
  ota::Version version;for(const char* text:{"01.2.3","1.2","1.2.3-beta","1.2.3\n","65536.0.0","1.0.99999999999999999"})assert(!ota::version(text,version));
  assert(ota::allowedDownload("https://release-assets.githubusercontent.com/a"));assert(!ota::allowedDownload("http://github.com/a"));assert(!ota::allowedDownload("https://github.com.evil/a"));assert(!ota::allowedDownload("https://evil@github.com/a"));
  DynamicJsonDocument doc(4096);doc["version"]="1.0.1";doc["board"]=ota_config::kBoard;doc["sha256"]=std::string(64,'a');doc["url"]="https://github.com/test/clock/releases/download/v1.0.1/firmware.bin";doc["signature"]=std::string(344,'A');doc["size"]=2048;
  ota::Manifest manifest;assert(ota::parseManifest(doc.as<JsonVariantConst>(),"test/clock",0x1E0000,manifest));assert(ota::canonical(manifest).find("1.0.1\n")==0);
  doc["size"]=0;assert(!ota::parseManifest(doc.as<JsonVariantConst>(),"test/clock",0x1E0000,manifest));doc["size"]=2048;doc["board"]="esp32s3";assert(!ota::parseManifest(doc.as<JsonVariantConst>(),"test/clock",0x1E0000,manifest));doc["board"]=ota_config::kBoard;doc["url"]="https://github.com/attacker/clock/releases/download/v1.0.1/firmware.bin";assert(!ota::parseManifest(doc.as<JsonVariantConst>(),"test/clock",0x1E0000,manifest));
}
int main(int argc,char**argv){assert(argc==4);coreTests();fixtures(argv[1],uint32_t(strtoul(argv[2],nullptr,10)),uint32_t(strtoul(argv[3],nullptr,10)));otaTests();std::cout<<"PASS: core, all live quote/history fixtures, malformed input, OTA manifest rules\n";}
