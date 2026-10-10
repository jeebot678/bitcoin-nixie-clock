#pragma once
#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <string.h>
#include "DeviceConfig.h"
#include "PriceSources.h"

namespace btc {
inline bool due(uint32_t now, uint32_t deadline) { return int32_t(now - deadline) >= 0; }
inline bool validPrice(double price) { return std::isfinite(price) && price > 0 && price <= 1e9; }
inline bool freshTimestamp(uint32_t timestamp, uint32_t now, uint32_t maxAge = config::kQuoteMaxAgeSeconds) {
  return timestamp && timestamp <= uint64_t(now) + 10 && uint64_t(timestamp) + maxAge >= now;
}
inline bool priceDigits(double price, uint8_t digits[6]) {
  if (!validPrice(price) || price >= 999999.5) return false;
  uint32_t integer = uint32_t(std::floor(price + 0.5));
  for (int i = 5; i >= 0; --i) { digits[i] = integer % 10; integer /= 10; }
  return true;
}
inline int dialPosition(uint32_t mv, const uint16_t centers[5]) {
  for (int i = 0; i < 5; ++i)
    if (std::abs(int(mv) - int(centers[i])) <= config::kDialToleranceMv) return i;
  return -1; // Open switch/transition: retain last stable setting.
}
struct Dial {
  int stable = 2, candidate = -1;
  bool initialized = false;
  uint32_t since = 0;
  bool update(int value, uint32_t now) {
    if (candidate != value) { candidate = value; since = now; }
    if (value < 0 || uint32_t(now - since) < config::kDebounceMs) return false;
    bool changed = !initialized || stable != value;
    stable = value; initialized = true;
    return changed;
  }
};
struct SourceState {
  uint32_t availableAt = 0;
  uint8_t failures = 0;
};
class Rotation {
 public:
  SourceState states[kSourceCount] = {};
  int choose(uint32_t now, bool fxFresh) {
    for (size_t attempt = 0; attempt < kSourceCount; ++attempt) {
      size_t index = next_++ % kSourceCount;
      if ((!kSources[index].usdt || fxFresh) && due(now, states[index].availableAt)) return int(index);
    }
    return -1;
  }
  void started(size_t index, uint32_t now) { states[index].availableAt = now + kSources[index].minimumIntervalMs; }
  void success(size_t index) { states[index].failures = 0; }
  void failure(size_t index, uint32_t now, int status, uint32_t retryAfterMs = 0) {
    auto& state = states[index];
    state.failures = std::min<uint8_t>(state.failures + 1, 8);
    uint32_t delay = std::min<uint32_t>(600000, 5000U << (state.failures - 1));
    if (status == 429 || status == 418) delay = std::max<uint32_t>(delay, 60000);
    if (status == 401 || status == 403 || status == 404 || status == 451) delay = std::max<uint32_t>(delay, 3600000);
    delay = std::max(delay, std::min<uint32_t>(retryAfterMs, 86400000));
    state.availableAt = now + delay;
  }
 private:
  size_t next_ = 0;
};
struct FxRate {
  double usd = 0;
  uint32_t receivedAt = 0;
  bool fresh(uint32_t now) const { return std::isfinite(usd) && usd >= 0.5 && usd <= 1.5 && uint32_t(now - receivedAt) <= config::kFxMaxAgeMs; }
  bool normalize(double raw, bool usdt, uint32_t now, double& result) const {
    if (!validPrice(raw) || (usdt && !fresh(now))) return false;
    result = raw * (usdt ? usd : 1.0);
    return validPrice(result);
  }
};
// Require independent confirmation at startup and for jumps over 5%.
// Accept a genuine large move when two different providers agree within 1%.
class PriceGuard {
 public:
  double price = 0;
  uint32_t acceptedAt = 0;
  bool accept(double value, size_t source, uint32_t now) {
    if (!validPrice(value)) return false;
    if (price && uint32_t(now - acceptedAt) < 600000 && std::abs(value / price - 1) <= 0.05) {
      price = value; acceptedAt = now; pending_ = 0; return true;
    }
    if (pending_ && source != pendingSource_ && uint32_t(now - pendingAt_) <= 60000 && std::abs(value / pending_ - 1) <= 0.01) {
      price = value; acceptedAt = now; pending_ = 0; return true;
    }
    pending_ = value; pendingSource_ = source; pendingAt_ = now; return false;
  }
 private:
  double pending_ = 0;
  size_t pendingSource_ = 0;
  uint32_t pendingAt_ = 0;
};
constexpr size_t kMaxCandles = 300; // Largest range: 24 h of five-minute candles.
struct Sample { uint32_t timestamp; double price; };
struct History {
  Sample samples[kMaxCandles] = {};
  uint16_t count = 0;
  bool add(uint32_t time, double price) {
    if (!time || !validPrice(price) || count >= kMaxCandles || (count && time <= samples[count - 1].timestamp)) return false;
    samples[count++] = {time, price}; return true;
  }
};
struct Plot { uint8_t rows[21]; uint32_t valid = 0; };
inline Plot makePlot(const History& history, uint32_t now, uint32_t window, double live, uint32_t liveTime) {
  Plot plot = {};
  if (!window || now < window) return plot;
  const uint32_t start = now - window;
  double prices[21] = {};
  auto add = [&](uint32_t timestamp, double price) {
    if (timestamp < start || timestamp > now || !validPrice(price)) return;
    size_t column = std::min<size_t>(20, uint64_t(timestamp - start) * 21 / window);
    prices[column] = price; plot.valid |= 1U << column;
  };
  for (size_t i = 0; i < history.count; ++i) add(history.samples[i].timestamp, history.samples[i].price);
  // The live tick occupies its actual bucket. Gaps remain gaps during outages.
  if (liveTime) add(liveTime, live);
  double low = 1e12, high = 0;
  for (size_t i = 0; i < 21; ++i) if (plot.valid & (1U << i)) { low = std::min(low, prices[i]); high = std::max(high, prices[i]); }
  if (!plot.valid) return plot;
  double margin = std::max(10.0, (high - low) * 0.1);
  double center = (high + low) * 0.5;
  double half = std::max(center * 0.0005, (high - low) * 0.5 + margin);
  low = center - half; high = center + half;
  for (size_t i = 0; i < 21; ++i) if (plot.valid & (1U << i))
    plot.rows[i] = uint8_t(12 - std::min(12, std::max(0, int(std::lround((prices[i] - low) * 12 / (high - low))))));
  return plot;
}
inline uint32_t staleAfterMs(uint32_t refreshMs) { return std::max<uint32_t>(30000, refreshMs * 3); }
inline bool validCredentials(const char* ssid, const char* password) {
  size_t s = strlen(ssid), p = strlen(password);
  if (!s || s > 32 || (p && (p < 8 || p > 64))) return false;
  if (p == 64) for (size_t i = 0; i < p; ++i)
    if (!((password[i] >= '0' && password[i] <= '9') || (password[i] >= 'a' && password[i] <= 'f') || (password[i] >= 'A' && password[i] <= 'F'))) return false;
  return true;
}
}
