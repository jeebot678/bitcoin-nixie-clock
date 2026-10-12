#pragma once
#include "CandleChart.h"
#include "HistoryCache.h"

// Independent namespace: existing close-only caches are retained, never
// misinterpreted as OHLC candles or overwritten during an interrupted upgrade.
class CandleStore {
 public:
  bool begin(btc::CandleSeries (&series)[5],btc::CandleScale (&scales)[5]);
  bool save(uint8_t window,const btc::CandleSeries& series,const btc::CandleScale& scale,uint64_t epochMs);
  bool ready()const{return ready_;}
  uint64_t savedTimeMs(uint8_t window)const{return window<5?epoch_[window]:0;}
  uint32_t savedEpoch(uint8_t window)const{return window<5?uint32_t(epoch_[window]/1000):0;}
 private:
  bool ready_=false;int8_t slot_[5]={-1,-1,-1,-1,-1};uint32_t generation_[5]={};uint64_t epoch_[5]={};
};
