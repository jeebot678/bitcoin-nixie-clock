#pragma once
#include "SkitChart.h"
namespace photo {
constexpr uint32_t kPrice=456081;
inline btc::Plot chart(){
  const uint8_t closes[21]={11,11,10,11,10,10,10,9,10,9,9,8,9,8,7,7,6,5,4,2,1};
  btc::CandleFrame frame;frame.valid=0x1fffff;
  for(unsigned c=0;c<21;++c){
    uint8_t open=c?closes[c-1]:11,close=closes[c];
    frame.bodies[c]=btc::candleMask(open,close);
    uint8_t top=std::min(open,close),bottom=std::max(open,close);
    if(c==2||c==7||c==12||c==16||c==20){if(top) --top;}
    frame.wicks[c]=btc::candleMask(top,bottom);
  }
  return skit::connectedPlot(frame);
}
}
