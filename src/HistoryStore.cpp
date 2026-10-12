#include <Arduino.h>
#include <SPIFFS.h>
#include "HistoryStore.h"

namespace {
void pathFor(char path[24],uint8_t window,uint8_t slot,bool legacy=false){snprintf(path,24,legacy?"/history-%u-%u.bin":"/history-v2-%u-%u.bin",window,slot);}
bool readSlot(uint8_t window, uint8_t slot, btc::CacheMetadata& metadata, btc::HistoryView* history = nullptr,uint8_t fileWindow=255) {
  bool legacy=fileWindow!=255;if(fileWindow==255)fileWindow=window;
  char path[24]; pathFor(path, fileWindow, slot,legacy);
  File file = SPIFFS.open(path, "r");
  return file && btc::readHistoryCache(file, window, metadata, history,fileWindow);
}
}
bool HistoryStore::beginViews(btc::HistoryView* histories[5]) {
  bool migrated[5]={};
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
    histories[window]->count = 0;
    // The retained 5 min / 30 min / 1 h ranges moved from old positions 0/1/2
    // to 2/3/4. Validate their existing range/step/CRC without rewriting dates.
    if(slot_[window]<0&&window>=2){
      uint8_t oldWindow=window-2;
      for(uint8_t slot=0;slot<2;++slot){btc::CacheMetadata candidate;
        if(readSlot(window,slot,candidate,nullptr,oldWindow)&&
            (slot_[window]<0||int32_t(candidate.generation-metadata_[window].generation)>0)){
          slot_[window]=slot;metadata_[window]=candidate;migrated[window]=true;
        }
      }
    }
    if (slot_[window] >= 0 && readSlot(window, slot_[window], metadata_[window], histories[window],migrated[window]?window-2:255))
      Serial.printf("History restored: %s, %u candles, saved epoch %lu\n", config::kWindowNames[window], histories[window]->count, (unsigned long)metadata_[window].epoch);
  }
  // Load every old range before saving any new slot that shares its path.
  for(uint8_t window=2;window<5;++window)if(migrated[window]&&histories[window]->count)save(window,*histories[window],metadata_[window].epoch);
  return true;
}
bool HistoryStore::save(uint8_t window, const btc::HistoryView& history, uint32_t epoch) {
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
