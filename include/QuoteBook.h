#pragma once
#include "ClockCore.h"

namespace btc {
// Fast-display motion changes only whole-dollar rendering, at most $1 from
// the real source quote. Chart values and quote timestamps remain intact.
inline uint32_t displayDollars(double price,uint32_t previous,bool motion,bool& cosmetic) {
  if (!validPrice(price) || price>=999999.5) {cosmetic=false;return 0;}
  uint32_t nearest=uint32_t(std::floor(price+0.5));cosmetic=false;
  if (!motion || nearest!=previous) return nearest;
  uint32_t adjacent=price>=nearest?nearest+1:nearest?nearest-1:nearest;
  if (adjacent && adjacent<=999999 && std::abs(double(adjacent)-price)<=1.0) {cosmetic=true;return adjacent;}
  return nearest;
}
struct DisplayQuote {
  double price=0;
  uint32_t receivedAt=0, timestamp=0;
  uint8_t source=0;
  DisplayQuote()=default;
  DisplayQuote(double p,uint32_t received,uint32_t stamp,uint8_t provider)
    :price(p),receivedAt(received),timestamp(stamp),source(provider) {}
};
// Keep one accepted quote per provider; a busy feed cannot erase the others.
class QuoteBook {
 public:
  static constexpr uint32_t kMaximumAgeMs=5000;
  void put(double price,uint8_t source,uint32_t receivedAt,uint32_t timestamp) {
    if (source<kSourceCount && validPrice(price)) entries_[source]={price,receivedAt,timestamp,source};
  }
  bool choose(uint32_t now,double previous,uint8_t previousSource,double reference,
              bool fxFresh,DisplayQuote& result) const {
    const uint32_t previousDigits=validPrice(previous)?uint32_t(std::floor(previous+0.5)):0;
    // Prefer real quotes that change the whole-dollar digits. Never invent a
    // price or rejuvenate its receipt time when exchanges round alike.
    for (unsigned changed=0;changed<2;++changed) {
      for (uint32_t age:{1500U,kMaximumAgeMs}) {
        for (size_t offset=1;offset<=kSourceCount;++offset) {
          size_t index=(previousSource+offset)%kSourceCount;
          const auto& quote=entries_[index];
          if (!validPrice(quote.price) || uint32_t(now-quote.receivedAt)>age ||
              (kSources[index].usdt && !fxFresh) || !validPrice(reference) ||
              std::abs(quote.price/reference-1)>0.05) continue;
          if (!changed && uint32_t(std::floor(quote.price+0.5))==previousDigits) continue;
          result=quote;return true;
        }
      }
    }
    return false;
  }
  void clear() { for (auto& entry:entries_) entry={}; }
 private:
  DisplayQuote entries_[kSourceCount]={};
};
}
