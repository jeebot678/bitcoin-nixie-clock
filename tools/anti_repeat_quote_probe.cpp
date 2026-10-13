// Computer-side live probe of the firmware parsers, guard and sequence policy.
// Input: C/D/H at source epoch; M at source epoch JSON; F at epoch entropy.
// Connection liveness and fallback orchestration are modeled, never device SPI.
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include "QuoteBook.h"
#include "StreamParsers.h"

int main(){
  btc::QuoteBook quotes;btc::QuoteSequence sequence;btc::PriceGuard guard;
  btc::BitfinexBook book,usdtBook,fxBook;btc::DisplayQuote shown,lastAccepted;
  btc::BitfinexBook* books[]={&book,&usdtBook,&fxBook};uint32_t bookMessageAt[3]={};
  auto name=[](btc::QuoteKind kind){return kind==btc::QuoteKind::ConvertedBboMidpoint?"converted-bbo-midpoint":kind==btc::QuoteKind::BboMidpoint?"bbo-midpoint":"last-trade";};
  bool connected[btc::kSourceCount]={};uint32_t lastMessage[btc::kSourceCount]={};
  auto current=[&](unsigned position,uint32_t now){return connected[4]&&books[position]->valid()&&uint32_t(now-bookMessageAt[position])<=35000;};
  uint32_t lastQuoteReceived=0,received[btc::kSourceCount]={},accepted[btc::kSourceCount]={};
  StaticJsonDocument<8192> document;
  std::string line;
  std::cout<<std::setprecision(15);
  while(std::getline(std::cin,line)){
    std::istringstream input(line);char command;uint32_t now,epoch;
    if(!(input>>command>>now))continue;
    if(command!='F'){
      unsigned source;if(!(input>>source>>epoch)||source>=btc::kSourceCount)continue;
      if(command=='C'){connected[source]=true;lastMessage[source]=now;if(source==4)for(auto item:books)item->reset();continue;}
      if(command=='D'){connected[source]=false;if(source==4)for(auto item:books)item->reset();continue;}
      if(command=='H'){lastMessage[source]=now;continue;}
      if(command!='M')continue;
      std::string payload;std::getline(input,payload);document.clear();
      if(deserializeJson(document,payload)){if(source==4){for(auto item:books)item->corrupted=true;quotes.invalidateCurrentBook(4);quotes.invalidateCurrentBook(4,btc::QuoteKind::ConvertedBboMidpoint);}continue;}
      lastMessage[source]=now;JsonVariantConst root=document.as<JsonVariantConst>();
      btc::Quote quote;bool valid=false;
      if(source==4){
        if(btc::equals(root["event"],"conf")){for(auto item:books)if(!item->configure(root))item->corrupted=true;continue;}
        if(btc::equals(root["event"],"subscribed")){
          const char* symbols[]={"tBTCUSD","tBTCUST","tUSTUSD"};bool bound=false;
          for(unsigned i=0;i<3;++i)if(btc::equals(root["symbol"],symbols[i])){bound=books[i]->bind(root,symbols[i]);break;}
          if(!bound)for(auto item:books)item->corrupted=true;continue;
        }
        if(btc::equals(root["event"],"unsubscribed")||btc::equals(root["event"],"error")){for(auto item:books)item->corrupted=true;continue;}
        if(!root.is<JsonArrayConst>()||!root[0].is<uint32_t>())continue;
        int position=-1;for(unsigned i=0;i<3;++i)if(books[i]->channel&&root[0].as<uint32_t>()==books[i]->channel){position=int(i);break;}
        if(position<0)continue;
        bookMessageAt[position]=now;valid=books[position]->apply(root,epoch,quote);
        if(books[position]->corrupted)for(auto item:books)item->corrupted=true;
        if(position>0){
          valid=valid&&current(1,now)&&current(2,now)&&fxBook.bid>=.5&&fxBook.ask<=1.5;
          if(valid){
            quote.price=(usdtBook.bid+usdtBook.ask)/2*((fxBook.bid+fxBook.ask)/2);
            quote.timestampMs=std::max(usdtBook.publishedAt,fxBook.publishedAt);
            quote.timestamp=uint32_t(quote.timestampMs/1000);quote.kind=btc::QuoteKind::ConvertedBboMidpoint;
          }
        }
      }else valid=btc::parseStreamQuote(uint8_t(source),root,epoch,quote);
      if(!valid)continue;
      ++received[source];bool allowed=guard.accept(quote.price,uint8_t(source),now);
      if(allowed){
        ++accepted[source];lastQuoteReceived=now;
        lastAccepted={quote.price,now,quote.timestamp,uint8_t(source),quote.kind};
        quotes.put(quote.price,uint8_t(source),now,quote.timestamp,quote.kind);
      }else if(quote.kind!=btc::QuoteKind::LastTrade)quotes.invalidateCurrentBook(uint8_t(source),quote.kind);
      std::cout<<"{\"type\":\"quote\",\"at\":"<<now<<",\"source\":"<<source
        <<",\"price\":"<<quote.price<<",\"timestamp_ms\":"<<quote.timestampMs
        <<",\"kind\":\""<<name(quote.kind)
        <<"\",\"accepted\":"<<(allowed?"true":"false")<<"}"<<std::endl;
      continue;
    }
    uint32_t entropy;if(!(input>>epoch>>entropy))continue;
    uint16_t currentBooks=current(0,now)?uint16_t(1U<<4):0;
    uint16_t currentConvertedBooks=current(1,now)&&current(2,now)&&fxBook.bid>=.5&&fxBook.ask<=1.5?uint16_t(1U<<4):0;
    bool stale=uint32_t(now-lastQuoteReceived)>btc::staleAfterMs(100),blank=!guard.price||(stale&&!currentBooks&&!currentConvertedBooks);
    btc::DisplayQuote selected=lastAccepted;bool chosen=false,held=false;
    uint32_t previous=shown.price?uint32_t(std::floor(shown.price+.5)):0;
    if(!blank){
      chosen=quotes.choose(now,previous,shown.source,guard.price,false,selected,currentBooks,&sequence,entropy,currentConvertedBooks);
      uint32_t fallback=uint32_t(std::floor(selected.price+.5));
      if(!chosen&&(stale||sequence.reverses(fallback))){
        if(!shown.price||uint32_t(now-shown.receivedAt)>btc::staleAfterMs(100)||std::abs(shown.price/guard.price-1)>.05)blank=true;
        else{selected=shown;held=true;}
      }
    }
    uint32_t digits=blank?0:uint32_t(std::floor(selected.price+.5));
    bool reversal=!blank&&sequence.reverses(digits);
    if(!blank){sequence.record(digits);shown=selected;}
    else shown={};
    std::cout<<"{\"type\":\"frame\",\"at\":"<<now<<",\"price\":"<<(blank?0:selected.price)
      <<",\"digits\":"<<digits<<",\"source\":"<<unsigned(blank?255:selected.source)
      <<",\"quote_age_ms\":"<<(blank?UINT32_MAX:uint32_t(now-selected.receivedAt))
      <<",\"timestamp\":"<<(blank?0:selected.timestamp)<<",\"kind\":\""
      <<name(blank?btc::QuoteKind::LastTrade:selected.kind)
      <<"\",\"current_books_mask\":"<<currentBooks<<",\"current_converted_books_mask\":"<<currentConvertedBooks<<",\"blank\":"<<(blank?"true":"false")
      <<",\"selected\":"<<(chosen?"true":"false")<<",\"fallback_hold\":"<<(held?"true":"false")
      <<",\"reversal\":"<<(reversal?"true":"false")<<",\"sequence_latest\":"<<sequence.latest()
      <<",\"coinbase_quotes\":"<<received[0]<<",\"bitfinex_quotes\":"<<received[4]
      <<",\"coinbase_accepted\":"<<accepted[0]<<",\"bitfinex_accepted\":"<<accepted[4]<<"}"<<std::endl;
  }
}
