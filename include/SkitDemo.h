#pragma once
#include <stdint.h>
#include <cmath>
namespace skit {
constexpr uint32_t kRefreshMs=500;
constexpr uint32_t kCycleMs=60000;
// Baseline 15s, pump 20s, reaction hold 10s, rug 3s, $1 hold 12s.
inline double price(uint32_t elapsed) {
  elapsed%=kCycleMs;
  if(elapsed<15000)return 81000+18*std::sin(elapsed*0.003);
  if(elapsed<35000){double t=(elapsed-15000)/20000.0;return 81000+969000*t*t;}
  if(elapsed<45000)return 1050000;
  if(elapsed<48000){double t=(elapsed-45000)/3000.0;return 1+1049999*std::pow(1-t,3);}
  return 1;
}
inline uint32_t digits(uint32_t elapsed){return uint32_t(std::fmin(999999,std::floor(price(elapsed)+0.5)));}
}
