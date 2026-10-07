#include <Arduino.h>
#include <SPI.h>

#include "LedMatrixMap.h"

namespace {

// Rev20 motherboard pin assignments.
constexpr uint8_t kMaxDataPin = 13;
constexpr uint8_t kMaxClockPin = 14;
constexpr uint8_t kMaxLoadPin = 27;
constexpr uint8_t kNixieClockPin = 18;
constexpr uint8_t kNixieDataPin = 23;
constexpr uint8_t kNixieCsPins[6] = {16, 17, 21, 22, 25, 26};

constexpr uint8_t kRegDigit0 = 0x01;
constexpr uint8_t kRegDecodeMode = 0x09;
constexpr uint8_t kRegIntensity = 0x0A;
constexpr uint8_t kRegScanLimit = 0x0B;
constexpr uint8_t kRegShutdown = 0x0C;
constexpr uint8_t kRegDisplayTest = 0x0F;

constexpr uint32_t kMaxSpiFrequency = 1000000;
constexpr uint8_t kMatrixIntensity = 2;
constexpr uint8_t kShiftOrder[led_matrix::kDrivers] = {3, 4, 5, 2, 1, 0};

constexpr uint8_t kExixePacketSize = 16;
constexpr uint8_t kExixeHeader = 0xAA;  // Normal current; overdrive disabled.
constexpr uint8_t kExixeEnable = 0x80;
constexpr uint8_t kNixieBrightness = 64;
constexpr uint32_t kNixieSpiFrequency = 1000000;

constexpr uint32_t kGridTestTimeMs = 750;
constexpr uint32_t kChartStartDelayMs = 400;
constexpr uint32_t kChartFrameTimeMs = 33;  // Approximately 30 frames/second.

// The chart uses the complete 13x21 matrix; no pixels are reserved for axes.
constexpr uint8_t kChartSamples = led_matrix::kColumns;
constexpr uint8_t kChartHeight = led_matrix::kRows;
constexpr int32_t kMinimumViewportHalfSpan = 200;
constexpr int32_t kViewportMargin = 60;

enum class TestStage : uint8_t { GridTest, Chart };

SPIClass maxBus(HSPI);
SPIClass nixieBus(VSPI);
uint8_t frame[led_matrix::kDrivers][8] = {};
int32_t priceHistory[kChartSamples] = {};
uint8_t lastNixieDigits[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

TestStage stage = TestStage::GridTest;
uint8_t gridUnderTest = 0;
uint32_t nextUpdateAt = 0;
int32_t simulatedPrice = 100000;
uint32_t randomState = 0xA17C9E3D;
uint32_t chartFrame = 0;
int32_t viewportCenter = 100000;
int32_t viewportHalfSpan = kMinimumViewportHalfSpan;
uint16_t framesUntilJump = 120;

void writeMaxRegisters(const uint8_t registers[led_matrix::kDrivers],
                       const uint8_t values[led_matrix::kDrivers]) {
  maxBus.beginTransaction(SPISettings(kMaxSpiFrequency, MSBFIRST, SPI_MODE0));
  digitalWrite(kMaxLoadPin, LOW);
  for (uint8_t slot = 0; slot < led_matrix::kDrivers; ++slot) {
    const uint8_t driver = kShiftOrder[slot];
    maxBus.transfer(registers[driver]);
    maxBus.transfer(values[driver]);
  }
  digitalWrite(kMaxLoadPin, HIGH);
  maxBus.endTransaction();
}

void writeAllMax(uint8_t reg, uint8_t value) {
  uint8_t registers[led_matrix::kDrivers];
  uint8_t values[led_matrix::kDrivers];
  for (uint8_t driver = 0; driver < led_matrix::kDrivers; ++driver) {
    registers[driver] = reg;
    values[driver] = value;
  }
  writeMaxRegisters(registers, values);
}

void flushFrame() {
  uint8_t registers[led_matrix::kDrivers];
  uint8_t values[led_matrix::kDrivers];
  for (uint8_t digit = 0; digit < 8; ++digit) {
    for (uint8_t driver = 0; driver < led_matrix::kDrivers; ++driver) {
      registers[driver] = kRegDigit0 + digit;
      values[driver] = frame[driver][digit];
    }
    writeMaxRegisters(registers, values);
  }
}

void clearFrame() {
  memset(frame, 0, sizeof(frame));
}

void setPixel(uint8_t row, uint8_t column) {
  const led_matrix::PixelAddress pixel = led_matrix::mapPixel(row, column);
  if (!pixel.valid) return;
  frame[pixel.driver][pixel.digit] |= uint8_t(1U << pixel.segmentBit);
}

void showGrid(uint8_t driver) {
  clearFrame();
  for (uint8_t row = 0; row < led_matrix::kRows; ++row) {
    for (uint8_t column = 0; column < led_matrix::kColumns; ++column) {
      const led_matrix::PixelAddress pixel = led_matrix::mapPixel(row, column);
      if (pixel.valid && pixel.driver == driver) setPixel(row, column);
    }
  }
  flushFrame();
}

void initializeMax7219Chain() {
  writeAllMax(kRegShutdown, 0x00);
  writeAllMax(kRegDisplayTest, 0x00);
  writeAllMax(kRegDecodeMode, 0x00);
  writeAllMax(kRegScanLimit, 0x07);
  writeAllMax(kRegIntensity, kMatrixIntensity);
  clearFrame();
  flushFrame();
  writeAllMax(kRegShutdown, 0x01);
}

void writeExixePacket(uint8_t module, const uint8_t packet[kExixePacketSize]) {
  if (module >= 6) return;
  nixieBus.beginTransaction(
      SPISettings(kNixieSpiFrequency, MSBFIRST, SPI_MODE0));
  digitalWrite(kNixieCsPins[module], LOW);
  delayMicroseconds(1);
  for (uint8_t i = 0; i < kExixePacketSize; ++i) {
    nixieBus.transfer(packet[i]);
  }
  delayMicroseconds(1);
  digitalWrite(kNixieCsPins[module], HIGH);
  nixieBus.endTransaction();
}

void setNixieDigit(uint8_t module, uint8_t digit) {
  uint8_t packet[kExixePacketSize];
  memset(packet, kExixeEnable, sizeof(packet));
  packet[0] = kExixeHeader;
  const uint8_t digitByte = (digit == 0) ? 10 : digit;
  packet[digitByte] = kExixeEnable | kNixieBrightness;
  writeExixePacket(module, packet);
}

void blankNixies() {
  uint8_t packet[kExixePacketSize];
  memset(packet, kExixeEnable, sizeof(packet));
  packet[0] = kExixeHeader;
  for (uint8_t module = 0; module < 6; ++module) {
    writeExixePacket(module, packet);
    lastNixieDigits[module] = 0xFF;
  }
}

void displayPriceOnNixies(uint32_t price, bool force = false) {
  price %= 1000000;
  uint8_t digits[6];
  for (int8_t position = 5; position >= 0; --position) {
    digits[position] = price % 10;
    price /= 10;
  }

  for (uint8_t module = 0; module < 6; ++module) {
    if (force || digits[module] != lastNixieDigits[module]) {
      setNixieDigit(module, digits[module]);
      lastNixieDigits[module] = digits[module];
    }
  }
}

uint32_t nextRandom() {
  randomState ^= randomState << 13;
  randomState ^= randomState >> 17;
  randomState ^= randomState << 5;
  return randomState;
}

void advanceSimulatedPrice() {
  int32_t delta = int32_t(nextRandom() % 71) - 35;
  delta += ((chartFrame / 120) & 1U) ? -6 : 6;

  // Add a conspicuous market jump every three to eight seconds. These are
  // deliberately in the requested $500-$1,000 range.
  if (framesUntilJump == 0) {
    const int32_t jump = 500 + int32_t(nextRandom() % 501);
    delta += (nextRandom() & 1U) ? jump : -jump;
    framesUntilJump = 90 + uint16_t(nextRandom() % 151);
  } else {
    --framesUntilJump;
  }

  simulatedPrice = constrain(simulatedPrice + delta, 50000, 950000);

  for (uint8_t i = 0; i + 1 < kChartSamples; ++i) {
    priceHistory[i] = priceHistory[i + 1];
  }
  priceHistory[kChartSamples - 1] = simulatedPrice;
  ++chartFrame;
}

void updateViewport() {
  int32_t low = priceHistory[0];
  int32_t high = priceHistory[0];
  for (uint8_t i = 1; i < kChartSamples; ++i) {
    low = min(low, priceHistory[i]);
    high = max(high, priceHistory[i]);
  }

  const int32_t activity = high - low;
  viewportCenter = low + activity / 2;

  // Follow the recent range closely so small moves remain visible. Expansion
  // is immediate after a jump; contraction is smoothed to avoid harsh flicker.
  const int32_t targetHalfSpan =
      max(kMinimumViewportHalfSpan, activity / 2 + kViewportMargin);
  if (targetHalfSpan >= viewportHalfSpan) {
    viewportHalfSpan = targetHalfSpan;
  } else {
    const int32_t shrink = max<int32_t>(1, (viewportHalfSpan - targetHalfSpan) / 4);
    viewportHalfSpan -= shrink;
  }
}

uint8_t priceToRow(int32_t price, int32_t low, int32_t high) {
  if (high <= low) return (kChartHeight - 1) / 2;
  price = constrain(price, low, high);
  const uint32_t scaled = (uint64_t(price - low) * (kChartHeight - 1)) /
                          uint32_t(high - low);
  return uint8_t((kChartHeight - 1) - scaled);
}

void drawStockChart() {
  clearFrame();

  updateViewport();
  const int32_t low = viewportCenter - viewportHalfSpan;
  const int32_t high = viewportCenter + viewportHalfSpan;

  // A strict one-pixel-per-column plot prevents steep changes from lighting
  // vertical stacks of LEDs.
  for (uint8_t sample = 0; sample < kChartSamples; ++sample) {
    const uint8_t row = priceToRow(priceHistory[sample], low, high);
    setPixel(row, sample);
  }

  flushFrame();
}

void seedPriceHistory() {
  simulatedPrice = 100000;
  chartFrame = 0;
  viewportCenter = simulatedPrice;
  viewportHalfSpan = kMinimumViewportHalfSpan;
  framesUntilJump = 120;
  for (uint8_t i = 0; i < kChartSamples; ++i) {
    // Seed a quiet history so the first deliberate jump occurs on-screen.
    const int32_t delta = int32_t(nextRandom() % 31) - 15;
    simulatedPrice += delta;
    priceHistory[i] = simulatedPrice;
  }
}

void initializeNixieBus() {
  pinMode(kNixieClockPin, OUTPUT);
  digitalWrite(kNixieClockPin, LOW);
  pinMode(kNixieDataPin, OUTPUT);
  digitalWrite(kNixieDataPin, LOW);
  for (uint8_t pin : kNixieCsPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  nixieBus.begin(kNixieClockPin, -1, kNixieDataPin, -1);
  blankNixies();
}

void runGridTest(uint32_t now) {
  if (int32_t(now - nextUpdateAt) < 0) return;

  ++gridUnderTest;
  if (gridUnderTest < led_matrix::kDrivers) {
    showGrid(gridUnderTest);
    nextUpdateAt = now + kGridTestTimeMs;
    return;
  }

  clearFrame();
  flushFrame();
  seedPriceHistory();
  stage = TestStage::Chart;
  nextUpdateAt = now + kChartStartDelayMs;
}

void runChart(uint32_t now) {
  if (int32_t(now - nextUpdateAt) < 0) return;

  advanceSimulatedPrice();
  drawStockChart();
  displayPriceOnNixies(uint32_t(simulatedPrice));
  nextUpdateAt = now + kChartFrameTimeMs;
}

}  // namespace

void setup() {
  initializeNixieBus();

  pinMode(kMaxLoadPin, OUTPUT);
  digitalWrite(kMaxLoadPin, HIGH);
  maxBus.begin(kMaxClockPin, -1, kMaxDataPin, kMaxLoadPin);
  initializeMax7219Chain();

  // Run U1 through U6 individually before beginning the chart simulation.
  gridUnderTest = 0;
  showGrid(gridUnderTest);
  nextUpdateAt = millis() + kGridTestTimeMs;
}

void loop() {
  const uint32_t now = millis();
  if (stage == TestStage::GridTest) runGridTest(now);
  else runChart(now);
}
