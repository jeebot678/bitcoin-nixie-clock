#pragma once
#include "ClockCore.h"

namespace btc {
inline uint32_t firstHistoryClose(uint8_t window, uint32_t epoch) {
  if (window >= 5 || epoch < config::kMinimumEpoch) return 0;
  uint32_t step = config::kCandleSeconds[window];
  return uint32_t((uint64_t(epoch - config::kWindowSeconds[window]) + step - 1) / step * step);
}
// Return the opening time of the FIRST missing closed candle. This also repairs
// gaps in the middle, rather than assuming the newest cached point proves coverage.
inline uint32_t missingHistoryStart(const History& history, uint8_t window, uint32_t epoch) {
  uint32_t first = firstHistoryClose(window, epoch);
  if (!first) return 0;
  uint32_t step = config::kCandleSeconds[window], last = epoch / step * step;
  size_t index = 0;
  for (uint64_t close = first; close <= last; close += step) {
    while (index < history.count && history.samples[index].timestamp < close) ++index;
    if (index == history.count || history.samples[index].timestamp != close) return uint32_t(close - step);
  }
  return 0;
}
inline bool trimHistory(History& history, uint8_t window, uint32_t epoch) {
  uint32_t first = firstHistoryClose(window, epoch);
  if (!first) return false; // Never age stored data against an unsynchronized clock.
  uint16_t count = 0;
  for (size_t i = 0; i < history.count; ++i) {
    const auto& sample = history.samples[i];
    if (sample.timestamp >= first && sample.timestamp <= epoch)
      history.samples[count++] = sample;
  }
  bool changed = count != history.count;
  history.count = count;
  return changed;
}
inline bool mergeHistory(History& stored, const History& incoming, uint8_t window, uint32_t epoch) {
  bool changed = trimHistory(stored, window, epoch);
  uint32_t first = firstHistoryClose(window, epoch);
  if (!first) return changed;
  for (size_t i = 0; i < incoming.count; ++i) {
    const auto& sample = incoming.samples[i];
    if (sample.timestamp < first || sample.timestamp > epoch ||
        sample.timestamp % config::kCandleSeconds[window] || !validPrice(sample.price)) continue;
    size_t index = 0;
    while (index < stored.count && stored.samples[index].timestamp < sample.timestamp) ++index;
    if (index < stored.count && stored.samples[index].timestamp == sample.timestamp) {
      if (stored.samples[index].price != sample.price) { stored.samples[index] = sample; changed = true; }
    } else if (stored.count < kMaxCandles) {
      memmove(stored.samples + index + 1, stored.samples + index, (stored.count - index) * sizeof(Sample));
      stored.samples[index] = sample; ++stored.count; changed = true;
    }
  }
  return changed;
}

// Explicit little-endian wire format, with no compiler padding or raw structs.
// One header + 12 bytes per candle. CRC covers the header AND every sample.
struct CacheMetadata { uint32_t generation = 0, epoch = 0; };
constexpr size_t kCacheHeaderSize = 32, kCacheSampleSize = 12;
inline void cachePut32(uint8_t* p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(n >> (8 * i)); }
inline uint32_t cacheGet32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint32_t cacheCrc(uint32_t crc, const uint8_t* data, size_t size) {
  while (size--) { crc ^= *data++; for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U))); }
  return crc;
}
inline void cacheSampleBytes(const Sample& sample, uint8_t bytes[kCacheSampleSize]) {
  static_assert(sizeof(double) == sizeof(uint64_t), "64-bit prices required");
  cachePut32(bytes, sample.timestamp);
  uint64_t price; memcpy(&price, &sample.price, sizeof(price));
  for (unsigned i = 0; i < 8; ++i) bytes[4 + i] = uint8_t(price >> (8 * i));
}
inline bool validCacheSample(const Sample& sample, uint8_t window, uint32_t epoch, uint32_t previous) {
  return sample.timestamp >= firstHistoryClose(window, epoch) && sample.timestamp <= epoch &&
    sample.timestamp % config::kCandleSeconds[window] == 0 && sample.timestamp > previous && validPrice(sample.price);
}
template<class Writer>
bool writeHistoryCache(Writer& writer, const History& history, uint8_t window, const CacheMetadata& metadata) {
  if (window >= 5 || !history.count || history.count > kMaxCandles || metadata.epoch < config::kMinimumEpoch) return false;
  uint8_t header[kCacheHeaderSize] = {}, bytes[kCacheSampleSize];
  cachePut32(header, 0x48435442); // BTCH
  header[4] = 1; header[5] = window;
  cachePut32(header + 8, config::kWindowSeconds[window]); cachePut32(header + 12, config::kCandleSeconds[window]);
  cachePut32(header + 16, metadata.generation); cachePut32(header + 20, metadata.epoch); cachePut32(header + 24, history.count);
  uint32_t crc = cacheCrc(UINT32_MAX, header, 28), previous = 0;
  for (size_t i = 0; i < history.count; ++i) {
    if (!validCacheSample(history.samples[i], window, metadata.epoch, previous)) return false;
    previous = history.samples[i].timestamp;
    cacheSampleBytes(history.samples[i], bytes); crc = cacheCrc(crc, bytes, sizeof(bytes));
  }
  cachePut32(header + 28, crc ^ UINT32_MAX);
  if (writer.write(header, sizeof(header)) != sizeof(header)) return false;
  for (size_t i = 0; i < history.count; ++i) {
    cacheSampleBytes(history.samples[i], bytes);
    if (writer.write(bytes, sizeof(bytes)) != sizeof(bytes)) return false;
  }
  return true;
}
template<class Reader>
bool readHistoryCache(Reader& reader, uint8_t window, CacheMetadata& metadata, History* history = nullptr) {
  if (history) history->count = 0;
  if (window >= 5) return false;
  uint8_t header[kCacheHeaderSize], bytes[kCacheSampleSize];
  if (reader.readBytes(reinterpret_cast<char*>(header), sizeof(header)) != sizeof(header)) return false;
  uint32_t count = cacheGet32(header + 24), epoch = cacheGet32(header + 20);
  if (cacheGet32(header) != 0x48435442 || header[4] != 1 || header[5] != window || header[6] || header[7] ||
      cacheGet32(header + 8) != config::kWindowSeconds[window] || cacheGet32(header + 12) != config::kCandleSeconds[window] ||
      !count || count > kMaxCandles || epoch < config::kMinimumEpoch || reader.size() != kCacheHeaderSize + count * kCacheSampleSize) return false;
  uint32_t crc = cacheCrc(UINT32_MAX, header, 28), previous = 0;
  for (size_t i = 0; i < count; ++i) {
    if (reader.readBytes(reinterpret_cast<char*>(bytes), sizeof(bytes)) != sizeof(bytes)) return false;
    crc = cacheCrc(crc, bytes, sizeof(bytes));
    Sample sample = {cacheGet32(bytes), 0}; uint64_t price = 0;
    for (unsigned j = 0; j < 8; ++j) price |= uint64_t(bytes[4 + j]) << (8 * j);
    memcpy(&sample.price, &price, sizeof(price));
    if (!validCacheSample(sample, window, epoch, previous)) return false;
    previous = sample.timestamp;
    if (history) history->samples[i] = sample;
  }
  if ((crc ^ UINT32_MAX) != cacheGet32(header + 28)) return false;
  metadata.generation = cacheGet32(header + 16); metadata.epoch = epoch;
  if (history) history->count = uint16_t(count);
  return true;
}
}
