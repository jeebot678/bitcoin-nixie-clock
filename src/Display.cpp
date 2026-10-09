#include <Arduino.h>
#include <SPI.h>
#include "Display.h"
#include "LedMatrixMap.h"
#include "MatrixText.h"

namespace {
SPIClass matrix(HSPI), tubes(VSPI);
uint8_t frame[6][8] = {}, previous[6][8] = {};
uint8_t lastDigits[6] = {255,255,255,255,255,255};
bool frameKnown = false;
bool textActive = false, chasing = false;
char messageTop[32] = {}, messageBottom[32] = {};
uint32_t messageStartedAt = 0, nextZeroAt = 0;
uint8_t zeroPosition = 0;
void registers(const uint8_t regs[6], const uint8_t values[6]) {
  matrix.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(config::kMaxLoad, LOW);
  for (uint8_t slot : config::kMaxShiftOrder) { matrix.transfer(regs[slot]); matrix.transfer(values[slot]); }
  digitalWrite(config::kMaxLoad, HIGH); matrix.endTransaction();
}
void all(uint8_t reg, uint8_t value) {
  uint8_t regs[6], values[6]; memset(regs,reg,6); memset(values,value,6); registers(regs,values);
}
void flush() {
  if (frameKnown && !memcmp(frame,previous,sizeof(frame))) return;
  for (uint8_t digit=0;digit<8;++digit) {
    uint8_t regs[6],values[6];
    for (uint8_t d=0;d<6;++d) { regs[d]=digit+1; values[d]=frame[d][digit]; }
    registers(regs,values);
  }
  memcpy(previous,frame,sizeof(frame)); frameKnown=true;
}
void pixel(uint8_t row,uint8_t col) {
  auto p=led_matrix::mapPixel(row,col);
  if (p.valid) frame[p.driver][p.digit] |= uint8_t(1U<<p.segmentBit);
}
void packet(uint8_t module,const uint8_t data[16]) {
  for (uint8_t cs : config::kNixieCs) digitalWrite(cs,HIGH);
  tubes.beginTransaction(SPISettings(1000000,MSBFIRST,SPI_MODE0));
  delayMicroseconds(10); digitalWrite(config::kNixieCs[module],LOW); delayMicroseconds(1);
  for (uint8_t i=0;i<16;++i) tubes.transfer(data[i]);
  delayMicroseconds(1); digitalWrite(config::kNixieCs[module],HIGH); delayMicroseconds(10);
  tubes.endTransaction();
}
void tubeDigit(uint8_t module, uint8_t digit) {
  if (lastDigits[module] == digit) return;
  uint8_t data[16]; memset(data,0x80,sizeof(data)); data[0]=0xAA;
  if (digit < 10) data[digit == 0 ? 10 : digit] = 0x80 | config::kNixieBrightness;
  packet(module,data); lastDigits[module]=digit;
}
}
namespace display {
void begin() {
  // Set every CS latch inactive before enabling any SPI output.
  for (uint8_t pin : config::kNixieCs) digitalWrite(pin,HIGH);
  for (uint8_t pin : config::kNixieCs) pinMode(pin,OUTPUT);
  digitalWrite(config::kNixieClock,LOW); pinMode(config::kNixieClock,OUTPUT);
  digitalWrite(config::kNixieData,LOW); pinMode(config::kNixieData,OUTPUT);
  tubes.begin(config::kNixieClock,-1,config::kNixieData,-1);
  blankPrice();
  digitalWrite(config::kMaxLoad,HIGH); pinMode(config::kMaxLoad,OUTPUT);
  matrix.begin(config::kMaxClock,-1,config::kMaxData,config::kMaxLoad);
  all(0x0C,0); all(0x0F,0); all(0x09,0); all(0x0B,7); all(0x0A,config::kMatrixIntensity);
  memset(frame,0,sizeof(frame)); flush(); all(0x0C,1);
}
void blankPrice() {
  chasing=false;
  for (uint8_t i=0;i<6;++i) tubeDigit(i,254);
}
void price(double usd) {
  chasing=false;
  uint8_t digits[6];
  if (!btc::priceDigits(usd,digits)) { blankPrice(); return; }
  for (uint8_t i=0;i<6;++i) tubeDigit(i,digits[i]);
}
void setupZero(uint32_t now) {
  if (!chasing) {
    chasing=true; zeroPosition=0; nextZeroAt=now+config::kSetupNixieStepMs;
  } else if (btc::due(now,nextZeroAt)) {
    uint32_t steps=uint32_t(now-nextZeroAt)/config::kSetupNixieStepMs+1;
    zeroPosition=uint8_t((zeroPosition+steps)%6);
    nextZeroAt+=steps*config::kSetupNixieStepMs;
  }
  // Blank every other tube before lighting the next zero. Never show two zeros.
  for (uint8_t i=0;i<6;++i) if (i!=zeroPosition) tubeDigit(i,254);
  tubeDigit(zeroPosition,0);
}
void plot(const btc::Plot& chart) {
  textActive=false;
  memset(frame,0,sizeof(frame));
  for (uint8_t col=0;col<21;++col) if (chart.valid&(1U<<col)) pixel(chart.rows[col],col);
  flush();
}
void offline() {
  textActive=false;
  memset(frame,0,sizeof(frame));
  for (uint8_t i=0;i<7;++i) {
    pixel(3+i,7+i); pixel(3+i,13-i);
  }
  flush();
}
void message(const char* top,const char* bottom,uint32_t now) {
  if (!top) top="";
  if (!bottom) bottom="";
  if (!textActive || strcmp(top,messageTop) || strcmp(bottom,messageBottom)) {
    snprintf(messageTop,sizeof(messageTop),"%s",top);
    snprintf(messageBottom,sizeof(messageBottom),"%s",bottom);
    messageStartedAt=now; textActive=true;
  }
  memset(frame,0,sizeof(frame));
  uint32_t elapsed=uint32_t(now-messageStartedAt);
  matrix_text::drawLine(messageTop,*messageBottom?1:4,elapsed,pixel);
  if (*messageBottom) matrix_text::drawLine(messageBottom,7,elapsed,pixel);
  flush();
}
void setupStatus(btc::WifiSetupState state,uint32_t now) {
  switch (state) {
    case btc::WifiSetupState::Waiting: message("CONNECT","WIFI",now); break;
    case btc::WifiSetupState::Connecting: message("CONNECTING","",now); break;
    case btc::WifiSetupState::Connected: message("CONNECTED","",now); break;
    case btc::WifiSetupState::ConnectionFailed: message("WIFI","FAILED",now); break;
    case btc::WifiSetupState::SaveFailed: message("SAVE","FAILED",now); break;
    case btc::WifiSetupState::ApFailed: message("AP","ERROR",now); break;
  }
}
void selfTest(uint8_t driver) {
  textActive=false;
  memset(frame,0,sizeof(frame));
  for (uint8_t r=0;r<13;++r) for (uint8_t c=0;c<21;++c) if (led_matrix::mapPixel(r,c).driver==driver) pixel(r,c);
  flush();
}
}
