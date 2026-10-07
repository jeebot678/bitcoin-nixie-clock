#pragma once

#include <stdint.h>

namespace led_matrix {

constexpr uint8_t kRows = 13;
constexpr uint8_t kColumns = 21;
constexpr uint8_t kDrivers = 6;
constexpr uint16_t kLedCount = uint16_t(kRows) * kColumns;

struct PixelAddress {
  uint8_t driver;      // U1..U6 are represented by 0..5.
  uint8_t digit;       // MAX7219 DIG0..DIG7 / register 1..8.
  uint8_t segmentBit;  // A..G/DP encoded as MAX7219 bits 6,5,4,3,2,1,0,7.
  bool valid;
};

// These tables follow the routed PCB, viewed from the LED/component side with
// the Nixie tubes above the matrix. They intentionally are not sequential:
// the board routes columns to the MAX7219 segment pins in this order.
constexpr uint8_t kUpperFullBits[8] = {3, 7, 2, 4, 0, 5, 1, 6};
constexpr uint8_t kUpperRightBits[5] = {3, 2, 4, 5, 6};
constexpr uint8_t kLowerFullBits[8] = {6, 1, 5, 0, 4, 2, 7, 3};
constexpr uint8_t kLowerRightBits[5] = {6, 5, 0, 2, 3};

inline PixelAddress invalidPixel() { return {0, 0, 0, false}; }

inline PixelAddress mapPixel(uint8_t row, uint8_t column) {
  if (row >= kRows || column >= kColumns) return invalidPixel();

  if (row < 8) {
    if (column < 8) return {0, row, kUpperFullBits[column], true};       // U1
    if (column < 16) return {1, row, kUpperFullBits[column - 8], true}; // U2
    return {2, row, kUpperRightBits[column - 16], true};                // U3
  }

  const uint8_t digit = row - 8;
  if (column < 8) return {3, digit, kLowerFullBits[column], true};       // U4
  if (column < 16) return {4, digit, kLowerFullBits[column - 8], true}; // U5
  return {5, digit, kLowerRightBits[column - 16], true};                // U6
}

}  // namespace led_matrix
