#include <cassert>
#include <iostream>
#include "HistoryStore.h"
#include "SPIFFS.h"

SerialMock Serial;EspMock ESP;uint32_t testMillis=100;
std::map<int,int>pins,modes,adc;std::vector<GpioEvent>gpioEvents;
btc::History full(uint8_t window,uint32_t epoch){
  btc::History h;uint32_t step=config::kCandleSeconds[window];
  for(uint32_t close=btc::firstHistoryClose(window,epoch);close<=epoch;close+=step)assert(h.add(close,80000+(close%10000)*.1));
  return h;
}
void repairChecksum(std::vector<uint8_t>&file){btc::cachePut32(file.data()+28,btc::cacheCrc(btc::cacheCrc(UINT32_MAX,file.data(),28),file.data()+32,file.size()-32)^UINT32_MAX);}
int main(){
  const uint32_t now=1800000000;
  const uint32_t frequencies[]={500,2000,30000,300000,1800000},ranges[]={300,1800,3600,86400,604800};
  for(unsigned w=0;w<5;++w){assert(config::kRefreshMs[w]==frequencies[w]&&config::kWindowSeconds[w]==ranges[w]);}
  assert(config::kRefreshPin==34&&config::kTimelinePin==35);
  btc::History histories[5];HistoryStore store;assert(store.begin(histories));
  for(uint8_t w=0;w<5;++w){histories[w]=full(w,now);assert(store.save(w,histories[w],now));assert(!btc::missingHistoryStart(histories[w],w,now));}
  // A new instance models loss of all RAM. Every visited dial position restores.
  btc::History rebooted[5];HistoryStore afterRestart;assert(afterRestart.begin(rebooted));
  for(uint8_t w=0;w<5;++w){assert(rebooted[w].count==histories[w].count);assert(afterRestart.savedEpoch(w)==now);assert(btc::makePlot(rebooted[w],now,ranges[w],0,0).valid);}
  // A two-day outage retains the five-day overlap with its original timestamps,
  // requests only the missing 48 hourly candles, and merges without duplicates.
  const uint32_t later=now+2*86400;uint8_t w=4;
  assert(btc::trimHistory(rebooted[w],w,later));assert(rebooted[w].count==121);
  assert(btc::missingHistoryStart(rebooted[w],w,later)==now);
  auto missingPlot=btc::makePlot(rebooted[w],later,ranges[w],0,0);assert(!(missingPlot.valid&(1U<<20)));
  btc::History delta;for(uint32_t close=now+3600;close<=later;close+=3600)assert(delta.add(close,90000));
  assert(delta.count==48&&btc::mergeHistory(rebooted[w],delta,w,later));assert(rebooted[w].count==169);
  assert(!btc::missingHistoryStart(rebooted[w],w,later));assert(!btc::mergeHistory(rebooted[w],delta,w,later));
  assert(afterRestart.save(w,rebooted[w],later));
  // A hole in the middle is repaired even when the newest candle exists.
  btc::History gap=full(2,now);uint32_t hole=gap.samples[12].timestamp;
  memmove(gap.samples+12,gap.samples+13,(gap.count-13)*sizeof(btc::Sample));--gap.count;
  assert(btc::missingHistoryStart(gap,2,now)==hole-60);
  btc::History single;assert(single.add(hole,83000));assert(btc::mergeHistory(gap,single,2,now));assert(!btc::missingHistoryStart(gap,2,now));
  // Boundary and clock handling: no aging against epoch 0, no shifting dates,
  // expired windows require a full fetch, and a one-candle delta is sufficient.
  btc::History shortWindow=full(0,now);assert(!btc::trimHistory(shortWindow,0,0));
  assert(!btc::missingHistoryStart(shortWindow,0,now+59));assert(btc::missingHistoryStart(shortWindow,0,now+60)==now);
  assert(btc::trimHistory(shortWindow,0,later)&&!shortWindow.count);
  assert(btc::missingHistoryStart(shortWindow,0,later)==btc::firstHistoryClose(0,later)-60);
  // Interrupted saves at every header/payload offset leave the prior snapshot.
  auto committed=fsMock().files;auto next=full(0,now+60);
  for(size_t cut=0;cut<btc::kCacheHeaderSize+next.count*btc::kCacheSampleSize;++cut){
    fsMock().files=committed;fsMock().writeBudget=SIZE_MAX;
    btc::History copy[5];HistoryStore writer;assert(writer.begin(copy));fsMock().writeBudget=cut;
    assert(!writer.save(0,next,now+60));fsMock().writeBudget=SIZE_MAX;
    HistoryStore reader;assert(reader.begin(copy));assert(reader.savedEpoch(0)==now&&copy[0].count==histories[0].count);
  }
  fsMock().files=committed;fsMock().writeBudget=SIZE_MAX;
  btc::History copy[5];HistoryStore writer;writer.begin(copy);assert(writer.save(0,next,now+60));
  auto pristine=fsMock().files;auto newest=pristine["/history-0-1.bin"];
  for(size_t offset=0;offset<newest.size();++offset){
    fsMock().files=pristine;fsMock().files["/history-0-1.bin"][offset]^=1;
    HistoryStore reader;reader.begin(copy);assert(reader.savedEpoch(0)==now);
  }
  // Even correctly checksummed records reject wrong schema/range, out-of-order,
  // future/misaligned timestamps, invalid prices and excessive counts.
  for(unsigned mutation=0;mutation<7;++mutation){
    auto bad=newest;
    if(mutation==0)bad[4]=2;
    if(mutation==1)btc::cachePut32(bad.data()+8,86400);
    if(mutation==2)btc::cachePut32(bad.data()+32,now+120);
    if(mutation==3)btc::cachePut32(bad.data()+32,now-299);
    if(mutation==4)memset(bad.data()+36,0,8);
    if(mutation==5)btc::cachePut32(bad.data()+44,btc::cacheGet32(bad.data()+32));
    if(mutation==6)btc::cachePut32(bad.data()+24,301);
    repairChecksum(bad);fsMock().files=pristine;fsMock().files["/history-0-1.bin"]=bad;
    HistoryStore reader;reader.begin(copy);assert(reader.savedEpoch(0)==now);
  }
  fsMock().failMount=true;HistoryStore unavailable;assert(!unavailable.begin(copy));assert(!unavailable.save(0,next,now+60));fsMock().failMount=false;
  fsMock().files=committed;HistoryStore failedWrite;failedWrite.begin(copy);fsMock().failOpen=true;assert(!failedWrite.save(0,next,now+60));fsMock().failOpen=false;
  // Generation rollover must still select the later intact slot.
  File old("/history-0-0.bin","w");btc::CacheMetadata metadata;metadata.epoch=now;metadata.generation=UINT32_MAX;
  assert(btc::writeHistoryCache(old,histories[0],0,metadata));old.close();
  fsMock().files.erase("/history-0-1.bin");
  HistoryStore rollover;rollover.begin(copy);assert(rollover.save(0,next,now+60));HistoryStore finalBoot;finalBoot.begin(copy);assert(finalBoot.savedEpoch(0)==now+60);assert(btc::cacheGet32(fsMock().files["/history-0-1.bin"].data()+16)==0);
  std::cout<<"PASS: all five persistent ranges; reboot restore; 2-day incremental repair; gaps, duplicates, clock boundaries, every truncated-write/corrupted-byte offset, invalid records, storage failures and generation rollover\n";
}
