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
  const uint16_t originalLadder[5]={0,503,926,1336,1800};
  for(uint32_t measured:{142U,155U,177U,182U,216U,222U,237U,240U,301U,309U,342U})
    assert(btc::dialPosition(measured,originalLadder)==0);
  assert(btc::dialPosition(343,originalLadder)==1);
  // Fresh physical lowest-to-highest sweep: independently captured bands on
  // GPIO35 (frequency) and GPIO34 (range), not generated from the config.
  const uint16_t observedFrequency[5][2]={{1766,1776},{1326,1330},{912,917},{488,493},{142,142}};
  const uint16_t observedRange[5][2]={{1767,1775},{1326,1330},{908,914},{489,495},{142,142}};
  for(int i=0;i<5;++i){
    for(uint32_t v=observedFrequency[i][0];v<=observedFrequency[i][1];++v)assert(btc::dialPosition(v,config::kRefreshMv)==i);
    for(uint32_t v=observedRange[i][0];v<=observedRange[i][1];++v)assert(btc::dialPosition(v,config::kTimelineMv)==i);
  }
  // All mechanical wiring orders must decode the same resistor voltages to
  // their configured positions, including the nonzero lowest detents observed
  // over Wi-Fi. Ground tolerance must follow its contact rather than index 0.
  uint16_t ordered[5]={0,503,926,1336,1800};unsigned permutations=0;
  do {
    for(int i=0;i<5;++i){
      assert(btc::dialPosition(ordered[i],ordered)==i);
      assert(btc::dialPosition(ordered[i]+150,ordered)==i);
      if(ordered[i])assert(btc::dialPosition(ordered[i]-150,ordered)==i);
      else for(uint32_t value:{142U,309U,342U})assert(btc::dialPosition(value,ordered)==i);
    }
    assert(btc::dialPosition(724,ordered)==-1);
    assert(btc::dialPosition(2269,ordered)==-1&&btc::dialPosition(3300,ordered)==-1);
    assert(btc::dialPosition(UINT32_MAX,ordered)==-1);++permutations;
  }while(std::next_permutation(ordered,ordered+5));
  assert(permutations==120);
  const uint16_t withoutGround[5]={1800,1336,926,503,2269};
  assert(btc::dialPosition(1774,withoutGround)==0&&btc::dialPosition(1329,withoutGround)==1);
  assert(btc::dialPosition(0,withoutGround)==-1&&btc::dialPosition(309,withoutGround)==-1);
  const uint16_t overlapping[5]={1000,1100,1800,2269,503};
  assert(btc::dialPosition(1049,overlapping)==0&&btc::dialPosition(1051,overlapping)==1);
  assert(btc::dialPosition(1050,overlapping)==-1);
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
  // Multi-digit real candidates win over a synthetic-looking $1 step, at any cadence.
  book.clear();book.put(84191.2,0,100,1800000000);book.put(84192.2,1,100,1800000000);book.put(84176.4,2,100,1800000000);
  assert(book.choose(101,84191,0,84191,false,selected)&&selected.price==84176.4);
  book.clear();book.put(84192,1,3000,1800000000);book.put(84176,2,100,1800000000);
  assert(book.choose(3100,84191,0,84191,false,selected)&&selected.price==84176); // Real multi-digit option inside 5 s outranks a one-digit-only newer quote.
  book.put(84166.4,2,120,1800000000);book.put(84166.4,2,130,1800000000);
  assert(book.choose(150,84166,2,84166,false,selected)&&std::floor(selected.price+0.5)!=84166);
  assert(!book.choose(9000,84166,2,84166,false,selected));
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
  // Five minute-close anchors render a line across their 21-column time span,
  // while a two-minute outage remains an actual open gap. Recent real samples
  // fill additional buckets without altering the persistent candle history.
  btc::History minutes;for(unsigned i=0;i<5;++i)assert(minutes.add(now-240+i*60,80000+i*50));
  auto joined=btc::makePlot(minutes,now,300,0,0);unsigned litColumns=0;
  for(unsigned c=0;c<21;++c){bool lit=false;for(unsigned r=0;r<13;++r)lit=lit||btc::plotPixel(joined,r,c);litColumns+=lit;}
  assert(litColumns>=17&&minutes.count==5);
  btc::History gap;assert(gap.add(now-240,80000));assert(gap.add(now-120,80100));auto broken=btc::makePlot(gap,now,300,0,0);
  assert(!broken.connectors[8]);
  btc::Sample liveSamples[2]={{now-30,80120},{now-10,80130}};
  auto dense=btc::makePlot(minutes,now,300,0,0,liveSamples,2);assert(dense.valid!=joined.valid&&minutes.count==5);
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
void currentBookCoreTests(){
  btc::QuoteBook book;btc::DisplayQuote selected;const uint16_t current=1U<<4;const uint32_t published=1800000000;
  book.put(80017.25,4,100,published,btc::QuoteKind::BboMidpoint);auto frozen=book;
  for(uint32_t now:{6100U,10100U,30100U}){
    assert(book.choose(now,80010,0,80010,false,selected,current)&&selected.source==4&&selected.price==80017.25);
    assert(selected.receivedAt==100&&selected.timestamp==published&&selected.kind==btc::QuoteKind::BboMidpoint);
    assert(!book.choose(now,80010,0,80010,false,selected,0));
    assert(!book.choose(now,80010,0,80010,false,selected,1U<<0));
    assert(!memcmp(&frozen,&book,sizeof(book))); // Selection never renews receipt/publication time.
  }
  book.clear();book.put(80027,4,100,published,btc::QuoteKind::BboMidpoint);book.put(80017,4,200,published+1,btc::QuoteKind::BboMidpoint);
  assert(book.choose(5100,80017,4,80010,false,selected,current)&&selected.price==80027); // Older BBO remains ordinary data until its original 5 s TTL.
  assert(book.choose(30100,80017,4,80010,false,selected,current)&&selected.price==80017&&selected.receivedAt==200&&selected.timestamp==published+1);
  book.put(80017.2,4,300,published+2,btc::QuoteKind::BboMidpoint); // Same rounded price replaces the head.
  assert(book.choose(30100,80017,4,80010,false,selected,current)&&selected.price==80017.2&&selected.receivedAt==300&&selected.timestamp==published+2);
  book.put(80030,4,400,published+3,btc::QuoteKind::LastTrade);
  assert(book.choose(30100,80010,0,80010,false,selected,current)&&selected.price==80017.2&&selected.kind==btc::QuoteKind::BboMidpoint); // A trade event cannot erase the independent, still-valid native book ledger.
  book.clear();book.put(80017,0,100,published,btc::QuoteKind::LastTrade);
  assert(!book.choose(30100,80010,0,80010,false,selected,1U<<0));
  book.clear();book.put(80017,5,100,published,btc::QuoteKind::BboMidpoint);
  assert(!book.choose(30100,80010,0,80010,false,selected,1U<<5));
  assert(book.choose(30100,80010,0,80010,true,selected,1U<<5)); // Valid current books still require fresh FX.
  book.clear();book.put(87000,4,100,published,btc::QuoteKind::BboMidpoint);
  assert(!book.choose(30100,80010,0,80010,true,selected,current)); // Current state cannot override the price-spread guard.
  book.clear();book.put(80017,4,UINT32_MAX-100,published,btc::QuoteKind::BboMidpoint);
  assert(book.choose(6000,80010,0,80010,false,selected,current)&&selected.receivedAt==UINT32_MAX-100&&selected.timestamp==published);
  assert(!book.choose(6000,80010,0,80010,false,selected,0));
  book.clear();book.put(80027,4,100,published,btc::QuoteKind::BboMidpoint);book.put(80017,4,200,published+1,btc::QuoteKind::BboMidpoint);
  book.invalidateCurrentBook(4);book.invalidateCurrentBook(255);
  assert(book.choose(350,80010,0,80010,false,selected,current)&&selected.price==80027&&selected.receivedAt==100); // Historical entries retain only their ordinary TTL.
  assert(!book.choose(6100,80010,0,80010,false,selected,current)); // A rejected new current book cannot revalidate its prior publication.
  book.put(80015,4,6200,published+2,btc::QuoteKind::BboMidpoint);
  assert(book.choose(30100,80010,0,80010,false,selected,current)&&selected.price==80015&&selected.receivedAt==6200&&selected.timestamp==published+2);
  book.clear();book.put(80015,0,100,published,btc::QuoteKind::LastTrade);book.invalidateCurrentBook(0);
  assert(book.choose(101,80010,4,80010,false,selected)&&selected.price==80015); // Invalidating book state cannot remove a genuine trade head.
  std::cout<<"PASS: only a validated current latest BBO head outlives its original 5 s cache TTL; historical BBO/trades, original timestamps, FX, spread bounds and rollover remain enforced\n";
}
void quoteSequenceCoreTests(){
  const double real[4]={83111.25,83222.25,83333.25,83444.25};const uint32_t published=1800000000;
  btc::QuoteBook book;btc::DisplayQuote selected;btc::QuoteSequence sequence;
  for(unsigned i=0;i<4;++i)book.put(real[i],i,100,published,btc::QuoteKind::LastTrade);
  auto frozen=book;sequence.record(83111);std::set<uint32_t> tieWinners;
  for(uint32_t entropy=0;entropy<256;++entropy){
    assert(book.choose(200,83111,0,83333,false,selected,0,&sequence,entropy));
    uint32_t digits=uint32_t(std::floor(selected.price+.5));assert(digits!=83111);tieWinners.insert(digits);
    assert(selected.source<4&&selected.price==real[selected.source]&&selected.receivedAt==100&&selected.timestamp==published);
  }
  assert(tieWinners.size()==3&&!memcmp(&frozen,&book,sizeof(book))); // Entropy resolves genuine equal-recency ties without mutating market data.
  sequence.clear();uint32_t last=0,previous=0;uint8_t source=0;std::set<uint32_t> visited;
  for(uint32_t frame=0;frame<64;++frame){
    assert(book.choose(200,last,source,83333,false,selected,0,&sequence,frame*2654435761U));
    uint32_t digits=uint32_t(std::floor(selected.price+.5));assert(digits!=last&&digits!=previous);
    if(last)previous=last;last=digits;source=selected.source;sequence.record(digits);visited.insert(digits);
    if(frame==3)assert(visited.size()==4);
    assert(selected.price==real[selected.source]&&selected.receivedAt==100&&selected.timestamp==published);
  }
  assert(!memcmp(&frozen,&book,sizeof(book)));
  // Previous DISTINCT remains forbidden through repeated holds; with only two
  // eligible values the actual current quote is held until a third arrives.
  book.clear();sequence.clear();book.put(83111.25,0,100,published);book.put(83222.25,1,100,published);
  sequence.record(83111);assert(book.choose(200,83111,0,83333,false,selected,0,&sequence,1)&&selected.price==83222.25);sequence.record(83222);
  for(unsigned frame=0;frame<20;++frame){
    assert(book.choose(200,83222,1,83333,false,selected,0,&sequence,frame)&&selected.price==83222.25);
    sequence.record(83222);assert(sequence.reverses(83111)&&sequence.latest()==83222&&selected.receivedAt==100);
  }
  assert(!book.choose(5201,83222,1,83333,false,selected,0,&sequence,1)); // Sequence policy never resurrects expired trades.
  book.put(83333.25,2,5200,published+5);assert(book.choose(5201,83222,1,83333,false,selected,0,&sequence,1)&&selected.price==83333.25);
  // Unseen real values outrank a larger-digit move to a recently used price.
  book.clear();sequence.clear();sequence.record(84191);sequence.record(84176);sequence.record(84299);
  book.put(84191,0,100,published);book.put(84176,1,100,published);book.put(84299,2,100,published);book.put(84298,3,100,published);
  assert(book.choose(200,84299,2,84299,false,selected,0,&sequence,1)&&selected.price==84298);
  book.clear();sequence.clear();sequence.record(83111);book.put(83333.25,4,100,published,btc::QuoteKind::BboMidpoint);
  assert(book.choose(30100,83111,0,83333,false,selected,1U<<4,&sequence,1)&&selected.source==4&&selected.receivedAt==100&&selected.timestamp==published);
  assert(!book.choose(30100,83111,0,83333,false,selected,0,&sequence,1));
  book.invalidateCurrentBook(4);assert(!book.choose(30100,83111,0,83333,false,selected,1U<<4,&sequence,1));
  book.put(83333.25,5,100,published,btc::QuoteKind::BboMidpoint);
  assert(!book.choose(30100,83111,0,83333,false,selected,1U<<5,&sequence,1));
  assert(book.choose(30100,83111,0,83333,true,selected,1U<<5,&sequence,1));
  assert(!book.choose(30100,83111,0,160000,true,selected,1U<<5,&sequence,1));
  book.clear();book.put(83333.25,4,UINT32_MAX-100,published,btc::QuoteKind::BboMidpoint);
  assert(book.choose(6000,83111,0,83333,false,selected,1U<<4,&sequence,1)&&selected.receivedAt==UINT32_MAX-100&&selected.timestamp==published);
  std::cout<<"PASS: nonreversing real-price sequences, distinct-history holds, randomized genuine ties, unused-price preference, immutable timestamps and unchanged TTL/current-book/FX/spread/rollover checks\n";
}
void convertedBookCoreTests(){
  const uint16_t source4=1U<<4;const uint32_t epoch=1800000000;btc::QuoteBook book;btc::DisplayQuote selected;
  book.put(80017.2,4,100,epoch,btc::QuoteKind::BboMidpoint);
  book.put(80017.4,4,200,epoch+1,btc::QuoteKind::ConvertedBboMidpoint); // Same rounded value, different genuine provenance.
  assert(book.choose(30100,80000,0,80000,false,selected,source4,nullptr,0,0)&&selected.kind==btc::QuoteKind::BboMidpoint&&selected.price==80017.2&&selected.receivedAt==100&&selected.timestamp==epoch);
  assert(book.choose(30100,80000,0,80000,false,selected,0,nullptr,0,source4)&&selected.kind==btc::QuoteKind::ConvertedBboMidpoint&&selected.price==80017.4&&selected.receivedAt==200&&selected.timestamp==epoch+1);
  assert(!book.choose(30100,80000,0,80000,false,selected,0,nullptr,0,1U<<0)); // Exact provider/type mask, not any connected-book bit.
  assert(!book.choose(30100,80000,0,80000,false,selected));
  book.invalidateCurrentBook(4,btc::QuoteKind::ConvertedBboMidpoint);
  assert(!book.choose(30100,80000,0,80000,false,selected,0,nullptr,0,source4));
  assert(book.choose(30100,80000,0,80000,false,selected,source4,nullptr,0,source4)&&selected.kind==btc::QuoteKind::BboMidpoint&&selected.receivedAt==100);
  book.put(80018.4,4,300,epoch+2,btc::QuoteKind::ConvertedBboMidpoint);book.invalidateCurrentBook(4);
  assert(book.choose(30100,80000,0,80000,false,selected,source4,nullptr,0,source4)&&selected.kind==btc::QuoteKind::ConvertedBboMidpoint&&selected.receivedAt==300);
  // More than a full historical ring of events on one product must not evict
  // the other quiet product's actual current publication or alter its date.
  for(unsigned active=0;active<2;++active){
    book.clear();book.put(80017.2,4,100,epoch,btc::QuoteKind::BboMidpoint);book.put(80027.4,4,200,epoch+1,btc::QuoteKind::ConvertedBboMidpoint);
    auto kind=active?btc::QuoteKind::BboMidpoint:btc::QuoteKind::ConvertedBboMidpoint;
    for(unsigned i=0;i<btc::QuoteBook::kDepth*3;++i)book.put(80040.25+i,4,300+i,epoch+2+i,kind);
    auto frozen=book;uint16_t nativeMask=active?0:source4,convertedMask=active?source4:0;
    assert(book.choose(30100,80000,0,80000,false,selected,nativeMask,nullptr,0,convertedMask));
    assert(selected.kind==(active?btc::QuoteKind::ConvertedBboMidpoint:btc::QuoteKind::BboMidpoint));
    assert(selected.price==(active?80027.4:80017.2)&&selected.receivedAt==(active?200U:100U)&&selected.timestamp==epoch+active);
    btc::QuoteSequence sequence;sequence.record(80000);
    assert(book.choose(30100,80000,0,80000,false,selected,nativeMask,&sequence,42,convertedMask));
    assert(selected.receivedAt==(active?200U:100U)&&selected.timestamp==epoch+active&&!memcmp(&frozen,&book,sizeof(book)));
  }
  book.clear();book.put(80027.4,4,UINT32_MAX-100,epoch,btc::QuoteKind::ConvertedBboMidpoint);
  assert(book.choose(6000,80000,0,80000,false,selected,0,nullptr,0,source4)&&selected.receivedAt==UINT32_MAX-100&&selected.timestamp==epoch);
  assert(!book.choose(6000,80000,0,160000,false,selected,0,nullptr,0,source4));
  book.clear();assert(!book.choose(6000,80000,0,80000,false,selected,source4,nullptr,0,source4));
  std::cout<<"PASS: native/converted current-book ledgers preserve exact provenance independently through same-dollar quotes, per-kind invalidation, 36-event history eviction, mask separation, original timestamps, spread checks and rollover\n";
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
  const char* windows[]={"unused","unused","5m","30m","hour"};
  for(int provider=0;provider<2;++provider)for(uint8_t window=2;window<5;++window){
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
int main(int argc,char**argv){assert(argc==4);coreTests();currentBookCoreTests();quoteSequenceCoreTests();convertedBookCoreTests();fixtures(argv[1],uint32_t(strtoul(argv[2],nullptr,10)),uint32_t(strtoul(argv[3],nullptr,10)));otaTests();std::cout<<"PASS: core, all live quote/history fixtures, malformed input, OTA manifest rules\n";}
