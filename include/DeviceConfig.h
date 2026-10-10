#pragma once
#include <stdint.h>

namespace config {
constexpr uint8_t kMaxData = 13, kMaxClock = 14, kMaxLoad = 27;
constexpr uint8_t kNixieClock = 18, kNixieData = 23;
constexpr uint8_t kNixieCs[6] = {16, 17, 21, 22, 25, 26};
// Preserve the shift order proved by the working Rev20 demo.
constexpr uint8_t kMaxShiftOrder[6] = {3, 4, 5, 2, 1, 0};
// Rev20: SEL1_ADC -> U7.J2_5/IO34; SEL2_ADC -> U7.J2_6/IO35.
// JSEL1 = frequency, JSEL2 = range.
constexpr uint8_t kRefreshPin = 34, kTimelinePin = 35;
// First five contacts of the board's resistor ladder. Use `dials` over serial
// to measure the assembled selectors; adjust EACH dial independently here.
constexpr uint16_t kRefreshMv[5] = {0, 503, 926, 1336, 1800};
constexpr uint16_t kTimelineMv[5] = {0, 503, 926, 1336, 1800};
constexpr uint16_t kDialToleranceMv = 160;
constexpr uint32_t kDebounceMs = 150;
constexpr uint32_t kRefreshMs[5] = {500, 2000, 30000, 300000, 1800000};
constexpr uint32_t kWindowSeconds[5] = {300, 1800, 3600, 86400, 604800};
constexpr uint32_t kCandleSeconds[5] = {60, 60, 60, 300, 3600};
constexpr const char* kWindowNames[5] = {"5 minutes", "30 minutes", "1 hour", "24 hours", "7 days"};
constexpr uint32_t kHistoryRefreshMs[5] = {60000, 60000, 60000, 300000, 3600000};
constexpr uint32_t kHistorySaveRetryMs = 60000;
constexpr uint32_t kQuoteMaxAgeSeconds = 120;
constexpr uint32_t kFxMaxAgeMs = 120000, kFxRefreshMs = 60000;
constexpr uint32_t kConnectTimeoutMs = 25000, kRecoveryApMs = 120000;
constexpr uint8_t kMatrixIntensity = 2, kNixieBrightness = 64;
// 273 cells, about 0.82 s per pass. Group cells into nonblocking 16 ms frames.
constexpr uint32_t kStartupCellMs = 3, kStartupFrameMs = 16;
constexpr uint32_t kSetupFrameMs = 25, kSetupNixieStepMs = 100;
constexpr uint32_t kSetupScrollStepMs = 120, kSetupScrollPauseMs = 600;
constexpr uint32_t kMinimumEpoch = 1704067200;
}
