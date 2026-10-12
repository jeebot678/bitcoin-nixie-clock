#pragma once
#include "HistoryCache.h"

class HistoryStore {
 public:
  template<class History> bool begin(History (&histories)[5]){
    btc::HistoryView* views[5];for(unsigned i=0;i<5;++i)views[i]=&histories[i];return beginViews(views);
  }
  bool save(uint8_t window, const btc::HistoryView& history, uint32_t epoch);
  uint32_t savedEpoch(uint8_t window) const { return metadata_[window].epoch; }
  bool ready() const { return ready_; }
 private:
  bool beginViews(btc::HistoryView* histories[5]);
  btc::CacheMetadata metadata_[5];
  int8_t slot_[5] = {-1, -1, -1, -1, -1};
  bool ready_ = false;
};
