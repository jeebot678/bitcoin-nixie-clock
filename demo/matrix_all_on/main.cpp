#include <Arduino.h>
#include <SPI.h>
#include "DeviceConfig.h"
#include "../../src/LedMatrixMap.h"

namespace {
SPIClass matrix(HSPI), tubes(VSPI);

void writeRegisters(const uint8_t regs[6], const uint8_t values[6]) {
  matrix.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(config::kMaxLoad, LOW);
  for (uint8_t driver : config::kMaxShiftOrder) {
    matrix.transfer(regs[driver]);
    matrix.transfer(values[driver]);
  }
  digitalWrite(config::kMaxLoad, HIGH);
  matrix.endTransaction();
}

void writeAll(uint8_t reg, uint8_t value) {
  uint8_t regs[6], values[6];
  memset(regs, reg, sizeof(regs));
  memset(values, value, sizeof(values));
  writeRegisters(regs, values);
}

void blankTubes() {
  // Leave the tubes dark during this matrix-only test.
  for (uint8_t pin : config::kNixieCs) digitalWrite(pin, HIGH);
  for (uint8_t pin : config::kNixieCs) pinMode(pin, OUTPUT);
  digitalWrite(config::kNixieClock, LOW);
  pinMode(config::kNixieClock, OUTPUT);
  digitalWrite(config::kNixieData, LOW);
  pinMode(config::kNixieData, OUTPUT);
  tubes.begin(config::kNixieClock, -1, config::kNixieData, -1);
  for (uint8_t pin : config::kNixieCs) {
    tubes.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    delayMicroseconds(10);
    digitalWrite(pin, LOW);
    delayMicroseconds(1);
    tubes.transfer(0xAA);
    for (uint8_t i = 1; i < 16; ++i) tubes.transfer(0x80);
    delayMicroseconds(1);
    digitalWrite(pin, HIGH);
    delayMicroseconds(10);
    tubes.endTransaction();
  }
}
}

void setup() {
  Serial.begin(115200);
  blankTubes();
  digitalWrite(config::kMaxLoad, HIGH);
  pinMode(config::kMaxLoad, OUTPUT);
  matrix.begin(config::kMaxClock, -1, config::kMaxData, config::kMaxLoad);
  writeAll(0x0C, 0); // Configure while the matrix is shut down.
  writeAll(0x0F, 0); // Use normal brightness, not MAX7219 display-test mode.
  writeAll(0x09, 0);
  writeAll(0x0B, 7);
  writeAll(0x0A, config::kMatrixIntensity);

  uint8_t frame[6][8] = {};
  for (uint8_t row = 0; row < led_matrix::kRows; ++row) {
    for (uint8_t column = 0; column < led_matrix::kColumns; ++column) {
      const auto pixel = led_matrix::mapPixel(row, column);
      if (pixel.valid) frame[pixel.driver][pixel.digit] |= uint8_t(1U << pixel.segmentBit);
    }
  }
  for (uint8_t digit = 0; digit < 8; ++digit) {
    uint8_t regs[6], values[6];
    for (uint8_t driver = 0; driver < 6; ++driver) {
      regs[driver] = digit + 1;
      values[driver] = frame[driver][digit];
    }
    writeRegisters(regs, values);
  }
  writeAll(0x0C, 1);
  Serial.printf("Matrix all-on demo: %u LEDs held on; intensity %u. No Wi-Fi or OTA.\n",
                unsigned(led_matrix::kLedCount), unsigned(config::kMatrixIntensity));
}

void loop() {
  delay(1000); // The MAX7219 hardware holds the static frame indefinitely.
}
