#pragma once
#include "QuoteBook.h"

namespace btc {
// Display-only estimates stay anchored to a real quote, never to a previous
// estimate. At least three candidates remain even at six-digit boundaries.
class DisplayMotion {
 public:
  static uint32_t radius(double realPrice) {
    if(!validPrice(realPrice)||realPrice>=999999.5)return 0;
    return uint32_t(std::max(2.0,std::min(10.0,std::floor(realPrice*0.0001+0.5))));
  }
  static uint32_t choose(double realPrice,uint32_t current,const QuoteSequence& sequence,uint32_t entropy) {
    const uint32_t bound=radius(realPrice);if(!bound)return 0;
    const uint32_t low=uint32_t(std::max(1.0,std::ceil(realPrice-bound)));
    const uint32_t high=uint32_t(std::min(999999.0,std::floor(realPrice+bound)));
    uint32_t best=0,bestChance=0;unsigned bestRecent=0;bool bestMulti=false;
    for(uint32_t digits=low;digits<=high;++digits){
      if(digits==current||sequence.reverses(digits))continue;
      const bool multi=QuoteBook::changedDigits(current,digits)>=2;
      const unsigned recent=sequence.recency(digits);
      uint32_t chance=entropy^(digits*0x9e3779b9U);
      chance^=chance>>16;chance*=0x7feb352dU;chance^=chance>>15;chance*=0x846ca68bU;chance^=chance>>16;
      if(!best||multi>bestMulti||(multi==bestMulti&&recent>bestRecent)||
         (multi==bestMulti&&recent==bestRecent&&chance>bestChance)){
        best=digits;bestMulti=multi;bestRecent=recent;bestChance=chance;
      }
    }
    return best;
  }
};
}
