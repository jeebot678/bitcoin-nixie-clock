#pragma once
#include "ClockCore.h"

namespace display {
void begin();
void price(double usd);
void blankPrice();
void plot(const btc::Plot& plot);
void status(bool provisioning, bool offline);
void selfTest(uint8_t driver);
}
