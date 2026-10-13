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
  QuoteKind kind=QuoteKind::LastTrade;
  DisplayQuote()=default;
  DisplayQuote(double p,uint32_t received,uint32_t stamp,uint8_t provider,QuoteKind type=QuoteKind::LastTrade)
    :price(p),receivedAt(received),timestamp(stamp),source(provider),kind(type) {}
};
static_assert(sizeof(DisplayQuote)==24,"Quote kind must use existing display-cache padding");
// Keep several distinct real whole-dollar prices per provider. Duplicate
// updates refresh one entry instead of pushing every alternative out.
class QuoteBook {
 public:
  static constexpr uint32_t kMaximumAgeMs=5000;
  static constexpr unsigned kDepth=12;
  void put(double price,uint8_t source,uint32_t receivedAt,uint32_t timestamp,QuoteKind kind=QuoteKind::LastTrade) {
    if(source>=kSourceCount||!validPrice(price))return;
    auto& latest=entries_[source][heads_[source]];
    if(validPrice(latest.price)&&std::floor(latest.price+0.5)!=std::floor(price+0.5))heads_[source]=(heads_[source]+1)%kDepth;
    entries_[source][heads_[source]]={price,receivedAt,timestamp,source,kind};
  }
  static unsigned changedDigits(uint32_t a,uint32_t b){
    unsigned changed=0;for(unsigned i=0;i<6;++i){changed+=a%10!=b%10;a/=10;b/=10;}return changed;
  }
  bool choose(uint32_t now,double previous,uint8_t previousSource,double reference,
              bool fxFresh,DisplayQuote& result) const {
    const uint32_t previousDigits=validPrice(previous)?uint32_t(std::floor(previous+0.5)):0;
    // Prefer a genuine multi-digit change. Keep display deadlines independent
    // of fetching; if none differs, use a real quote and request another source.
    for(unsigned minimum:{2U,1U,0U})for(uint32_t age:{1500U,kMaximumAgeMs}){
      const DisplayQuote* best=nullptr;unsigned bestChanges=0;bool bestOther=false;uint32_t bestAge=UINT32_MAX;
      for(size_t offset=1;offset<=kSourceCount;++offset){
        size_t index=(previousSource+offset)%kSourceCount;
        for(const auto& quote:entries_[index]){
          uint32_t receiptAge=now-quote.receivedAt;
          if(!validPrice(quote.price)||quote.price>=999999.5||receiptAge>age||
              (kSources[index].usdt&&!fxFresh)||!validPrice(reference)||std::abs(quote.price/reference-1)>0.05)continue;
          unsigned differences=changedDigits(previousDigits,uint32_t(std::floor(quote.price+0.5)));
          if(differences<minimum)continue;
          bool other=index!=previousSource;
          if(!best||differences>bestChanges||(differences==bestChanges&&other>bestOther)||
              (differences==bestChanges&&other==bestOther&&receiptAge<bestAge)){
            best=&quote;bestChanges=differences;bestOther=other;bestAge=receiptAge;
          }
        }
      }
      if(best){result=*best;return true;}
    }
    return false;
  }
  void clear(){for(auto& source:entries_)for(auto& entry:source)entry={};memset(heads_,0,sizeof(heads_));}
 private:
  DisplayQuote entries_[kSourceCount][kDepth]={};
  uint8_t heads_[kSourceCount]={};
};
}
