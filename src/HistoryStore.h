#pragma once
#include "HistoryCache.h"

class HistoryStore {
 public:
  bool begin(btc::History histories[5]);
  bool save(uint8_t window, const btc::History& history, uint32_t epoch);
  uint32_t savedEpoch(uint8_t window) const { return metadata_[window].epoch; }
  bool ready() const { return ready_; }
 private:
  btc::CacheMetadata metadata_[5];
  int8_t slot_[5] = {-1, -1, -1, -1, -1};
  bool ready_ = false;
};
