#pragma once
#include <stdint.h>
#include <cmath>
namespace skit {
constexpr uint32_t kRefreshMs=500;
constexpr uint32_t kCycleMs=70000;
// Baseline 15s, pump 20s, reaction hold 10s, rug 3s, $1 hold 22s.
constexpr uint8_t kWindow=0; // 30 seconds, 21 candle columns.
inline double baseline(int32_t elapsed){
  return 81000+90*std::sin(elapsed*0.00032)+35*std::sin(elapsed*0.00091);
}
inline double price(uint32_t elapsed) {
  elapsed%=kCycleMs;
  if(elapsed<15000)return baseline(int32_t(elapsed));
  if(elapsed<35000){double t=(elapsed-15000)/20000.0;return baseline(15000)+(1050000-baseline(15000))*t*t;}
  if(elapsed<45000)return 1050000;
  if(elapsed<48000){double t=(elapsed-45000)/3000.0;return 1+1049999*std::pow(1-t,3);}
  return 1;
}
inline uint32_t digits(uint32_t elapsed){return uint32_t(std::fmin(999999,std::floor(price(elapsed)+0.5)));}
}
