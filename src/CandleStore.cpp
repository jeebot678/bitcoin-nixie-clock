#include <Arduino.h>
#include <SPIFFS.h>
#include "CandleStore.h"
namespace {
constexpr size_t headerSize=64,recordSize=28;
void pathFor(char* path,uint8_t window,uint8_t slot){snprintf(path,24,"/candles-%u-%u.bin",window,slot);}
void put64(uint8_t* p,uint64_t n){for(unsigned i=0;i<8;++i)p[i]=uint8_t(n>>(i*8));}
uint64_t get64(const uint8_t* p){uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(p[i])<<(i*8);return n;}
void putDouble(uint8_t* p,double d){uint64_t n;memcpy(&n,&d,8);put64(p,n);}
double getDouble(const uint8_t* p){uint64_t n=get64(p);double d;memcpy(&d,&n,8);return d;}
void encode(const btc::Candle& c,uint8_t* p){const uint32_t fields[]={c.bucket,c.openAt,c.closeAt,c.open,c.high,c.low,c.close};for(unsigned i=0;i<7;++i)btc::cachePut32(p+i*4,fields[i]);}
btc::Candle decode(const uint8_t* p){btc::Candle c;c.bucket=btc::cacheGet32(p);c.openAt=btc::cacheGet32(p+4);c.closeAt=btc::cacheGet32(p+8);c.open=btc::cacheGet32(p+12);c.high=btc::cacheGet32(p+16);c.low=btc::cacheGet32(p+20);c.close=btc::cacheGet32(p+24);return c;}
bool validBar(const btc::Candle& c,uint8_t w,uint64_t epochMs,unsigned index){
  if(!c.bucket){uint8_t bytes[recordSize];encode(c,bytes);for(auto b:bytes)if(b)return false;return true;}
  uint32_t current=btc::candleBucket(epochMs,w);
  return btc::validCandle(c,w)&&c.bucket%21==index&&c.bucket<=current&&c.bucket+20>=current&&btc::candleStart(c.bucket,w)+c.closeAt<=epochMs;
}
bool readSlot(uint8_t w,uint8_t slot,uint32_t& generation,uint64_t& epochMs,btc::CandleSeries* series=nullptr,btc::CandleScale* scale=nullptr){
  char path[24];pathFor(path,w,slot);File file=SPIFFS.open(path,"r");
  if(!file||file.size()!=headerSize+21*recordSize)return false;
  uint8_t snapshot[headerSize+21*recordSize];if(file.readBytes(reinterpret_cast<char*>(snapshot),sizeof(snapshot))!=sizeof(snapshot))return false;
  const uint8_t* header=snapshot;
  uint64_t stamp=get64(header+16);
  if(btc::cacheGet32(header)!=0x43435442||header[4]!=1||header[5]!=w||header[6]||header[7]||btc::cacheGet32(header+8)!=config::kWindowSeconds[w]||stamp<uint64_t(config::kMinimumEpoch)*1000||stamp>uint64_t(UINT32_MAX)*1000+999)return false;
  for(unsigned i=48;i<60;++i)if(header[i])return false;
  btc::CandleScale candidate;candidate.low=getDouble(header+24);candidate.step=getDouble(header+32);candidate.smoothedSpan=getDouble(header+40);if(!candidate.valid())return false;
  uint32_t crc=btc::cacheCrc(UINT32_MAX,header,60);btc::CandleSeries staged;
  for(unsigned i=0;i<21;++i){const uint8_t* record=snapshot+headerSize+i*recordSize;crc=btc::cacheCrc(crc,record,recordSize);staged.bars[i]=decode(record);if(!validBar(staged.bars[i],w,stamp,i))return false;}
  if((crc^UINT32_MAX)!=btc::cacheGet32(header+60)||!staged.count(w,stamp))return false;
  generation=btc::cacheGet32(header+12);epochMs=stamp;if(series)*series=staged;if(scale)*scale=candidate;return true;
}
}
bool CandleStore::begin(btc::CandleSeries (&series)[5],btc::CandleScale (&scales)[5]){
  ready_=SPIFFS.begin(true);if(!ready_)return false;
  for(uint8_t w=0;w<5;++w){slot_[w]=-1;generation_[w]=0;epoch_[w]=0;series[w]={};scales[w]={};
    for(uint8_t slot=0;slot<2;++slot){uint32_t generation=0;uint64_t stamp=0;
      if(readSlot(w,slot,generation,stamp)&&(slot_[w]<0||int32_t(generation-generation_[w])>0)){slot_[w]=slot;generation_[w]=generation;epoch_[w]=stamp;}}
    if(slot_[w]>=0)readSlot(w,uint8_t(slot_[w]),generation_[w],epoch_[w],&series[w],&scales[w]);
  }
  return true;
}
bool CandleStore::save(uint8_t w,const btc::CandleSeries& series,const btc::CandleScale& scale,uint64_t epochMs){
  if(!ready_||w>=5||!scale.valid()||!series.count(w,epochMs)||epochMs<uint64_t(config::kMinimumEpoch)*1000)return false;
  uint8_t snapshot[headerSize+21*recordSize]={};uint8_t* header=snapshot;btc::cachePut32(header,0x43435442);header[4]=1;header[5]=w;
  btc::cachePut32(header+8,config::kWindowSeconds[w]);btc::cachePut32(header+12,generation_[w]+1);put64(header+16,epochMs);
  putDouble(header+24,scale.low);putDouble(header+32,scale.step);putDouble(header+40,scale.smoothedSpan);
  uint32_t crc=btc::cacheCrc(UINT32_MAX,header,60);
  for(unsigned i=0;i<21;++i){uint8_t* record=snapshot+headerSize+i*recordSize;if(!validBar(series.bars[i],w,epochMs,i))return false;encode(series.bars[i],record);crc=btc::cacheCrc(crc,record,recordSize);}btc::cachePut32(header+60,crc^UINT32_MAX);
  uint8_t slot=slot_[w]==0?1:0;char path[24];pathFor(path,w,slot);File file=SPIFFS.open(path,"w");
  bool ok=file&&file.write(snapshot,sizeof(snapshot))==sizeof(snapshot);
  if(file){file.flush();file.close();}uint32_t generation=0;uint64_t stamp=0;
  if(!ok||!readSlot(w,slot,generation,stamp)||generation!=generation_[w]+1||stamp!=epochMs)return false;
  slot_[w]=slot;generation_[w]=generation;epoch_[w]=stamp;return true;
}
