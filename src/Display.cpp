#include <Arduino.h>
#include <SPI.h>
#include "Display.h"
#include "LedMatrixMap.h"

namespace {
SPIClass matrix(HSPI), tubes(VSPI);
uint8_t frame[6][8] = {}, previous[6][8] = {};
uint8_t lastDigits[6] = {255,255,255,255,255,255};
bool frameKnown = false;
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
  uint8_t data[16]; memset(data,0x80,sizeof(data)); data[0]=0xAA;
  for (uint8_t i=0;i<6;++i) if (lastDigits[i] != 254) { packet(i,data); lastDigits[i]=254; }
}
void price(double usd) {
  uint8_t digits[6];
  if (!btc::priceDigits(usd,digits)) { blankPrice(); return; }
  for (uint8_t i=0;i<6;++i) if (lastDigits[i]!=digits[i]) {
    uint8_t data[16]; memset(data,0x80,sizeof(data)); data[0]=0xAA;
    data[digits[i]==0 ? 10 : digits[i]]=0x80|config::kNixieBrightness;
    packet(i,data); lastDigits[i]=digits[i];
  }
}
void plot(const btc::Plot& chart) {
  memset(frame,0,sizeof(frame));
  for (uint8_t col=0;col<21;++col) if (chart.valid&(1U<<col)) pixel(chart.rows[col],col);
  flush();
}
void status(bool provisioning,bool offline) {
  memset(frame,0,sizeof(frame));
  // Centered + means Wi-Fi setup; X means no usable live price.
  for (uint8_t i=0;i<7;++i) {
    if (provisioning) { pixel(3+i,10); pixel(6,7+i); }
    else if (offline) { pixel(3+i,7+i); pixel(3+i,13-i); }
  }
  flush();
}
void selfTest(uint8_t driver) {
  memset(frame,0,sizeof(frame));
  for (uint8_t r=0;r<13;++r) for (uint8_t c=0;c<21;++c) if (led_matrix::mapPixel(r,c).driver==driver) pixel(r,c);
  flush();
}
}
