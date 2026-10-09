#pragma once
#include "ClockCore.h"
#include "WifiSetupState.h"

namespace display {
void begin();
void price(double usd);
void blankPrice();
void plot(const btc::Plot& plot);
void offline();
void message(const char* top, const char* bottom, uint32_t now);
void setupStatus(btc::WifiSetupState state, uint32_t now);
void setupZero(uint32_t now);
void selfTest(uint8_t driver);
}
