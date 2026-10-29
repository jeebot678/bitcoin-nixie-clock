#pragma once
#include <stdint.h>
#include <cmath>
namespace skit {
constexpr uint32_t kRefreshMs=500;
constexpr uint32_t kCycleMs=75000;
// Baseline 15s, pump 20s, reaction hold 10s, staged rug 8s, $1 hold 22s.
constexpr uint8_t kWindow=0; // 30 seconds, 21 candle columns.
inline double baseline(int32_t elapsed){
  return 81000+90*std::sin(elapsed*0.00032)+35*std::sin(elapsed*0.00091);
}
inline double price(uint32_t elapsed) {
  elapsed%=kCycleMs;
  if(elapsed<15000)return baseline(int32_t(elapsed));
  if(elapsed<35000){
    // Half-second observations: irregular surges and real pullbacks form wicks.
    static const uint32_t ramp[]={
      81000,83000,88000,85000,98000,110000,102000,125000,151000,138000,
      172000,205000,190000,240000,278000,252000,300000,345000,320000,390000,
      440000,410000,480000,540000,510000,590000,650000,610000,700000,760000,
      720000,810000,860000,825000,900000,940000,905000,975000,1020000,990000,1050000};
    uint32_t at=elapsed-15000,index=at/500;
    double from=index?ramp[index]:baseline(15000);
    return from+(ramp[index+1]-from)*(at%500)/500.0;
  }
  if(elapsed<45000)return 1050000;
  // Each level stays visible for four 500ms refreshes before the next plunge.
  if(elapsed<47000)return 10000+120*std::sin((elapsed-45000)*0.003);
  if(elapsed<49000)return 1000+18*std::sin((elapsed-47000)*0.003);
  if(elapsed<51000)return 100+3*std::sin((elapsed-49000)*0.003);
  if(elapsed<53000)return 5;
  return 1;
}
inline uint32_t digits(uint32_t elapsed){return uint32_t(std::fmin(999999,std::floor(price(elapsed)+0.5)));}
}
