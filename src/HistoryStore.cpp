#include <Arduino.h>
#include <SPIFFS.h>
#include "HistoryStore.h"

namespace {
void pathFor(char path[24], uint8_t window, uint8_t slot) { snprintf(path, 24, "/history-%u-%u.bin", window, slot); }
bool readSlot(uint8_t window, uint8_t slot, btc::CacheMetadata& metadata, btc::History* history = nullptr) {
  char path[24]; pathFor(path, window, slot);
  File file = SPIFFS.open(path, "r");
  return file && btc::readHistoryCache(file, window, metadata, history);
}
}
bool HistoryStore::begin(btc::History histories[5]) {
  // The existing 128 KiB data partition is separate from Wi-Fi NVS and both
  // OTA application slots. First use formats it; application uploads preserve it.
  ready_ = SPIFFS.begin(true);
  if (!ready_) { Serial.println("History storage unavailable; continuing with RAM history"); return false; }
  for (uint8_t window = 0; window < 5; ++window) {
    slot_[window] = -1; metadata_[window] = {};
    for (uint8_t slot = 0; slot < 2; ++slot) {
      btc::CacheMetadata candidate;
      if (readSlot(window, slot, candidate) &&
          (slot_[window] < 0 || int32_t(candidate.generation - metadata_[window].generation) > 0)) {
        slot_[window] = slot; metadata_[window] = candidate;
      }
    }
    histories[window].count = 0;
    if (slot_[window] >= 0 && readSlot(window, slot_[window], metadata_[window], &histories[window]))
      Serial.printf("History restored: %s, %u candles, saved epoch %lu\n", config::kWindowNames[window], histories[window].count, (unsigned long)metadata_[window].epoch);
  }
  return true;
}
bool HistoryStore::save(uint8_t window, const btc::History& history, uint32_t epoch) {
  if (!ready_ || window >= 5 || !history.count) return false;
  uint8_t slot = slot_[window] == 0 ? 1 : 0;
  btc::CacheMetadata candidate; candidate.generation = metadata_[window].generation + 1; candidate.epoch = epoch;
  char path[24]; pathFor(path, window, slot);
  File file = SPIFFS.open(path, "w");
  bool written = file && btc::writeHistoryCache(file, history, window, candidate);
  if (file) { file.flush(); file.close(); }
  // Never overwrite the previous valid slot. A truncated/corrupt new file is
  // ignored on reboot, including after interrupted writes or a full filesystem.
  btc::CacheMetadata verified;
  if (!written || !readSlot(window, slot, verified) || verified.generation != candidate.generation || verified.epoch != epoch) {
    Serial.printf("History save failed: %s; previous snapshot retained\n", config::kWindowNames[window]); return false;
  }
  slot_[window] = slot; metadata_[window] = candidate;
  Serial.printf("History saved: %s, %u candles\n", config::kWindowNames[window], history.count);
  return true;
}
