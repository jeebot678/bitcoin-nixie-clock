#pragma once
#include <stdint.h>

namespace config {
constexpr uint8_t kMaxData = 13, kMaxClock = 14, kMaxLoad = 27;
constexpr uint8_t kNixieClock = 18, kNixieData = 23;
constexpr uint8_t kNixieCs[6] = {16, 17, 21, 22, 25, 26};
// Preserve the shift order proved by the working Rev20 demo.
constexpr uint8_t kMaxShiftOrder[6] = {3, 4, 5, 2, 1, 0};
constexpr uint8_t kRefreshPin = 34, kTimelinePin = 35;
// First five contacts of the board's resistor ladder. Use `dials` over serial
// to measure the assembled selectors; adjust EACH dial independently here.
constexpr uint16_t kRefreshMv[5] = {0, 503, 926, 1336, 1800};
constexpr uint16_t kTimelineMv[5] = {0, 503, 926, 1336, 1800};
constexpr uint16_t kDialToleranceMv = 160;
constexpr uint32_t kDebounceMs = 150;
constexpr uint32_t kRefreshMs[5] = {2000, 5000, 15000, 60000, 300000};
constexpr uint32_t kWindowSeconds[5] = {3600, 86400, 604800, 2592000, 31536000};
constexpr uint32_t kCandleSeconds[5] = {60, 3600, 14400, 86400, 86400};
constexpr const char* kWindowNames[5] = {"1 hour", "1 day", "1 week", "30 days", "365 days"};
constexpr uint32_t kHistoryRefreshMs[5] = {60000, 300000, 900000, 3600000, 3600000};
constexpr uint32_t kQuoteMaxAgeSeconds = 120;
constexpr uint32_t kFxMaxAgeMs = 120000, kFxRefreshMs = 60000;
constexpr uint32_t kConnectTimeoutMs = 25000, kRecoveryApMs = 120000;
constexpr uint8_t kMatrixIntensity = 2, kNixieBrightness = 64;
constexpr uint32_t kMinimumEpoch = 1704067200;
}
