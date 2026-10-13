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
// Remember changes rather than frames: holding B after A must not permit a
// later return to A until a third distinct value has broken the two-price loop.
class QuoteSequence {
 public:
  static constexpr unsigned kDepth=8;
  uint32_t latest() const{return count_?values_[0]:0;}
  bool reverses(uint32_t digits) const{return count_>1&&digits!=values_[0]&&digits==values_[1];}
  unsigned recency(uint32_t digits) const{
    for(unsigned i=0;i<count_;++i)if(values_[i]==digits)return i;
    return kDepth;
  }
  void record(uint32_t digits){
    if(!digits||digits>999999||digits==latest())return;
    for(unsigned i=kDepth-1;i>0;--i)values_[i]=values_[i-1];
    values_[0]=digits;if(count_<kDepth)++count_;
  }
  void clear(){memset(values_,0,sizeof(values_));count_=0;}
 private:
  uint32_t values_[kDepth]={};uint8_t count_=0;
};
// Keep several distinct real whole-dollar prices per provider. Duplicate
// updates refresh one entry instead of pushing every alternative out.
class QuoteBook {
 public:
  static constexpr uint32_t kMaximumAgeMs=5000;
  static constexpr unsigned kDepth=12;
  void put(double price,uint8_t source,uint32_t receivedAt,uint32_t timestamp,QuoteKind kind=QuoteKind::LastTrade) {
    if(source>=kSourceCount||!validPrice(price))return;
    auto& latest=entries_[source][heads_[source]];
    if(validPrice(latest.price)&&(latest.kind!=kind||std::floor(latest.price+0.5)!=std::floor(price+0.5)))heads_[source]=(heads_[source]+1)%kDepth;
    entries_[source][heads_[source]]={price,receivedAt,timestamp,source,kind};
    if(source==4&&isBook(kind))currentBooks_[kind==QuoteKind::BboMidpoint?0:1]=entries_[source][heads_[source]];
  }
  static unsigned changedDigits(uint32_t a,uint32_t b){
    unsigned changed=0;for(unsigned i=0;i<6;++i){changed+=a%10!=b%10;a/=10;b/=10;}return changed;
  }
  bool choose(uint32_t now,double previous,uint8_t previousSource,double reference,
              bool fxFresh,DisplayQuote& result,uint16_t currentBooks=0,
              const QuoteSequence* sequence=nullptr,uint32_t random=0,uint16_t currentConvertedBooks=0) const {
    const uint32_t previousDigits=validPrice(previous)?uint32_t(std::floor(previous+0.5)):0;
    if(sequence){
      const DisplayQuote* best=nullptr;unsigned bestRecency=0,bestChanges=0;uint32_t bestRandom=0,bestAge=UINT32_MAX;
      for(size_t index=0;index<kSourceCount;++index)for(unsigned entry=0;entry<kDepth+(index==4?2U:0U);++entry){
        const auto& quote=entry<kDepth?entries_[index][entry]:currentBooks_[entry-kDepth];
        if(!eligible(quote,index,now,kMaximumAgeMs,reference,fxFresh,currentBooks,currentConvertedBooks))continue;
        uint32_t digits=uint32_t(std::floor(quote.price+0.5));
        if(sequence->reverses(digits))continue;
        unsigned recent=sequence->recency(digits),changes=changedDigits(previousDigits,digits);
        // Hash one fresh entropy value per frame and per rounded price. Duplicate
        // cache entries cannot give one number extra chances in the lottery.
        uint32_t chance=random^(digits*0x9e3779b9U);
        chance^=chance>>16;chance*=0x7feb352dU;chance^=chance>>15;chance*=0x846ca68bU;chance^=chance>>16;
        uint32_t age=now-quote.receivedAt;
        if(!best||recent>bestRecency||(recent==bestRecency&&changes>bestChanges)||
          (recent==bestRecency&&changes==bestChanges&&chance>bestRandom)||
          (recent==bestRecency&&changes==bestChanges&&chance==bestRandom&&age<bestAge)){
          best=&quote;bestRecency=recent;bestChanges=changes;bestRandom=chance;bestAge=age;
        }
      }
      if(best){result=*best;return true;}
      return false;
    }
    // Prefer a genuine multi-digit change. Keep display deadlines independent
    // of fetching; if none differs, use a real quote and request another source.
    for(unsigned minimum:{2U,1U,0U})for(uint32_t age:{1500U,kMaximumAgeMs}){
      const DisplayQuote* best=nullptr;unsigned bestChanges=0;bool bestOther=false;uint32_t bestAge=UINT32_MAX;
      for(size_t offset=1;offset<=kSourceCount;++offset){
        size_t index=(previousSource+offset)%kSourceCount;
        for(unsigned entry=0;entry<kDepth+(index==4?2U:0U);++entry){
          const auto& quote=entry<kDepth?entries_[index][entry]:currentBooks_[entry-kDepth];
          uint32_t receiptAge=now-quote.receivedAt;
          if(!eligible(quote,index,now,age,reference,fxFresh,currentBooks,currentConvertedBooks))continue;
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
  void clear(){for(auto& source:entries_)for(auto& entry:source)entry={};for(auto& book:currentBooks_)book={};memset(heads_,0,sizeof(heads_));}
  void invalidateCurrentBook(uint8_t source,QuoteKind kind=QuoteKind::BboMidpoint){
    if(source>=kSourceCount||!isBook(kind))return;
    if(source==4)currentBooks_[kind==QuoteKind::BboMidpoint?0:1]={};
    for(unsigned back=0;back<kDepth;++back){
      auto& entry=entries_[source][(heads_[source]+kDepth-back)%kDepth];
      if(validPrice(entry.price)&&entry.kind==kind){entry={};break;}
    }
  }
 private:
  static bool isBook(QuoteKind kind){return kind==QuoteKind::BboMidpoint||kind==QuoteKind::ConvertedBboMidpoint;}
  bool eligible(const DisplayQuote& quote,size_t index,uint32_t now,uint32_t age,
                double reference,bool fxFresh,uint16_t currentBooks,uint16_t currentConvertedBooks) const{
    // Only the synchronized book's latest midpoint can outlive its original
    // receipt TTL; selection never rewrites receipt/publication timestamps.
    uint16_t mask=quote.kind==QuoteKind::ConvertedBboMidpoint?currentConvertedBooks:currentBooks;
    const auto& head=index==4?currentBooks_[quote.kind==QuoteKind::BboMidpoint?0:1]:entries_[index][heads_[index]];
    bool current=isBook(quote.kind)&&(mask&(1U<<index))&&validPrice(head.price)&&
      quote.kind==head.kind&&quote.price==head.price&&quote.receivedAt==head.receivedAt&&quote.timestamp==head.timestamp;
    return validPrice(quote.price)&&quote.price<999999.5&&((now-quote.receivedAt)<=age||current)&&
      (!kSources[index].usdt||fxFresh)&&validPrice(reference)&&std::abs(quote.price/reference-1)<=0.05;
  }
  DisplayQuote entries_[kSourceCount][kDepth]={};
  // Keep two current source-4 book heads independently from their historical
  // ring: frequent updates on one product cannot evict a quiet live product.
  DisplayQuote currentBooks_[2];
  uint8_t heads_[kSourceCount]={};
};
}
