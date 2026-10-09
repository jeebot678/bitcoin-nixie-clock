#pragma once
#include <stdint.h>
#include <string.h>
#include "DeviceConfig.h"

namespace matrix_text {
// Three-column, five-row capitals; one blank column separates characters.
constexpr uint8_t kGlyphs[26][5] = {
  {2,5,7,5,5}, {6,5,6,5,6}, {3,4,4,4,3}, {6,5,5,5,6},
  {7,4,6,4,7}, {7,4,6,4,4}, {3,4,5,5,3}, {5,5,7,5,5},
  {7,2,2,2,7}, {1,1,1,5,2}, {5,5,6,5,5}, {4,4,4,4,7},
  {5,7,7,5,5}, {5,7,7,7,5}, {2,5,5,5,2}, {6,5,6,4,4},
  {2,5,5,7,3}, {6,5,6,5,5}, {3,4,2,1,6}, {7,2,2,2,2},
  {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5},
  {5,5,2,2,2}, {7,1,2,4,7}
};
inline uint16_t width(const char* text) {
  size_t count = text ? strlen(text) : 0;
  return count ? uint16_t(count * 4 - 1) : 0;
}
inline int16_t offset(uint16_t pixels, uint32_t elapsed) {
  if (pixels <= 21) return int16_t((21 - pixels) / 2);
  uint16_t distance = pixels - 21;
  uint32_t travel = uint32_t(distance) * config::kSetupScrollStepMs;
  uint32_t phase = elapsed % (2 * travel + 2 * config::kSetupScrollPauseMs);
  if (phase < config::kSetupScrollPauseMs) return 0;
  phase -= config::kSetupScrollPauseMs;
  if (phase < travel) return -int16_t(phase / config::kSetupScrollStepMs);
  phase -= travel;
  if (phase < config::kSetupScrollPauseMs) return -int16_t(distance);
  phase -= config::kSetupScrollPauseMs;
  return -int16_t(distance) + int16_t(phase / config::kSetupScrollStepMs);
}
template<class Sink>
void drawLine(const char* text, uint8_t firstRow, uint32_t elapsed, Sink sink) {
  if (!text) return;
  int16_t origin = offset(width(text), elapsed);
  for (size_t letter = 0; text[letter]; ++letter) {
    char ch = text[letter];
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    if (ch < 'A' || ch > 'Z') continue;
    for (uint8_t row = 0; row < 5 && firstRow + row < 13; ++row) {
      uint8_t bits = kGlyphs[ch - 'A'][row];
      for (uint8_t col = 0; col < 3; ++col) {
        int16_t x = origin + int16_t(letter * 4 + col);
        if ((bits & (1U << (2 - col))) && x >= 0 && x < 21)
          sink(uint8_t(firstRow + row), uint8_t(x));
      }
    }
  }
}
}
