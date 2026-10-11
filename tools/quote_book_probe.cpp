// Computer-side replay of the actual firmware guard and display selector.
#include <iostream>
#include <iomanip>
#include "QuoteBook.h"
int main(){
  btc::QuoteBook book;btc::PriceGuard guard;btc::DisplayQuote selected;
  double previous=0;unsigned previousSource=0;char kind;uint32_t at;
  while(std::cin>>kind>>at){
    if(kind=='Q'){
      unsigned source;double price;uint32_t timestamp;std::cin>>source>>price>>timestamp;
      if(source<btc::kSourceCount&&guard.accept(price,source,at))book.put(price,uint8_t(source),at,timestamp);
    }else if(kind=='F'&&book.choose(at,previous,uint8_t(previousSource),guard.price,false,selected)){
      previous=selected.price;previousSource=selected.source;
      std::cout<<std::setprecision(12)<<"{\"at\":"<<at<<",\"price\":"<<selected.price<<",\"source\":"<<unsigned(selected.source)<<",\"quote_age_ms\":"<<uint32_t(at-selected.receivedAt)<<",\"digits\":"<<uint32_t(std::floor(selected.price+0.5))<<"}"<<std::endl;
    }
  }
}
