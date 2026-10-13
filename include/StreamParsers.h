#pragma once
#include "MarketParsers.h"

namespace btc {
// A depth-one P0/F0 book gives a current USD quoted midpoint. Its optional
// outer TIMESTAMP is publication time; ticker FIRST_TRADE is never freshness.
class BitfinexBook {
 public:
  uint32_t channel=0;
  bool timestamps=false,ready=false,corrupted=false;
  double bid=0,ask=0;
  uint64_t publishedAt=0;
  void reset(){channel=0;timestamps=ready=corrupted=false;bid=ask=0;publishedAt=0;}
  bool configure(JsonVariantConst root){
    if(!equals(root["event"],"conf")||!equals(root["status"],"OK")||
      !root["flags"].is<uint32_t>()||!(root["flags"].as<uint32_t>()&32768U))return false;
    timestamps=true;return true;
  }
  bool bind(JsonVariantConst root){
    double length=0;
    if(!equals(root["event"],"subscribed")||!equals(root["channel"],"book")||
      !equals(root["symbol"],"tBTCUSD")||!equals(root["prec"],"P0")||!equals(root["freq"],"F0")||
      !number(root["len"],length)||length!=1||!root["chanId"].is<uint32_t>()||!root["chanId"].as<uint32_t>())return false;
    channel=root["chanId"].as<uint32_t>();ready=corrupted=false;bid=ask=0;publishedAt=0;return true;
  }
  bool apply(JsonVariantConst root,uint32_t epoch,Quote& quote){
    quote={};
    if(corrupted)return false;
    if(!channel||!root.is<JsonArrayConst>()||!root[0].is<uint32_t>()||root[0].as<uint32_t>()!=channel||equals(root[1],"hb"))return false;
    if(!timestamps||root.size()<3||!root[1].is<JsonArrayConst>()||!root[2].is<uint64_t>())return invalid();
    uint64_t stamp=root[2].as<uint64_t>(),now=uint64_t(epoch)*1000;
    if(!stamp||stamp/1000>UINT32_MAX||stamp>now+10000||stamp+5000<now||stamp<publishedAt)return invalid();
    JsonArrayConst rows=root[1].as<JsonArrayConst>();double nextBid=bid,nextAsk=ask;
    if(rows.size()&&rows[0].is<JsonArrayConst>()){
      if(rows.size()!=2)return invalid();
      nextBid=nextAsk=0;
      for(JsonVariantConst row:rows){
        double price=0,amount=0;uint32_t count=0;
        if(!entry(row,price,count,amount)||!count||(amount>0?nextBid:nextAsk))return invalid();
        (amount>0?nextBid:nextAsk)=price;
      }
      ready=true;
    }else{
      if(!ready)return invalid();
      double price=0,amount=0;uint32_t count=0;
      if(!entry(rows,price,count,amount))return invalid();
      double& side=amount>0?nextBid:nextAsk;
      if(count)side=price;
      else if(side==price)side=0; // Ignore removal of an old, replaced level.
    }
    bid=nextBid;ask=nextAsk;publishedAt=stamp;
    if(!validPrice(bid)||!validPrice(ask)||ask<bid||(ask-bid)/((ask+bid)/2)>0.01)return false;
    quote.price=(bid+ask)/2;quote.timestamp=uint32_t(stamp/1000);quote.timestampMs=stamp;quote.kind=QuoteKind::BboMidpoint;
    return validPrice(quote.price);
  }
 private:
  bool invalid(){corrupted=true;return false;}
  static bool entry(JsonVariantConst row,double& price,uint32_t& count,double& amount){
    if(!row.is<JsonArrayConst>()||row.size()<3||!number(row[0],price)||!validPrice(price)||
      !row[1].is<uint32_t>()||!number(row[2],amount)||!amount)return false;
    count=row[1].as<uint32_t>();return count||(amount==1||amount==-1);
  }
};
inline bool parseKrakenTrade(JsonVariantConst row,uint32_t epoch,Quote& quote,uint64_t& id) {
  quote={};id=0;
  if(!equals(row["symbol"],"BTC/USD")||!row["trade_id"].is<uint64_t>())return false;
  id=row["trade_id"].as<uint64_t>();
  quote.timestamp=isoTimestamp(row["timestamp"]);quote.timestampMs=isoTimestampMs(row["timestamp"]);
  return id&&number(row["price"],quote.price)&&validPrice(quote.price)&&freshTimestamp(quote.timestamp,epoch);
}
inline bool parseKrakenFx(JsonVariantConst root,uint32_t epoch,double& value) {
  value=0;
  if(!equals(root["channel"],"ticker")||
    (!equals(root["type"],"snapshot")&&!equals(root["type"],"update"))||
    !root["data"].is<JsonArrayConst>()||root["data"].size()!=1||
    !equals(root["data"][0]["symbol"],"USDT/USD")||
    !freshTimestamp(isoTimestamp(root["data"][0]["timestamp"]),epoch))return false;
  return number(root["data"][0]["last"],value)&&value>=0.5&&value<=1.5;
}
// Only authenticated TLS public BTC/USD messages are passed to this parser.
// A snapshot is a real last price, not a newly invented trade.
inline bool parseStreamQuote(uint8_t source, JsonVariantConst root, uint32_t epoch, Quote& quote, uint32_t channel=0) {
  quote={};
  JsonVariantConst value;
  if (source==0) {
    if (!equals(root["type"],"ticker") || !equals(root["product_id"],"BTC-USD")) return false;
    value=root["price"];quote.timestamp=isoTimestamp(root["time"]);quote.timestampMs=isoTimestampMs(root["time"]);
  } else if (source==1) {
    if (!equals(root["channel"],"ticker") ||
        (!equals(root["type"],"snapshot") && !equals(root["type"],"update")) ||
        !root["data"].is<JsonArrayConst>() || root["data"].size()!=1 ||
        !equals(root["data"][0]["symbol"],"BTC/USD")) return false;
    value=root["data"][0]["last"];quote.timestamp=isoTimestamp(root["data"][0]["timestamp"]);quote.timestampMs=isoTimestampMs(root["data"][0]["timestamp"]);
  } else if (source==2) {
    if (!equals(root["event"],"trade") || !equals(root["channel"],"live_trades_btcusd")) return false;
    value=root["data"]["price"];double timestamp=0;
    if (!number(root["data"]["microtimestamp"],timestamp) || timestamp<1 || timestamp>4.294967295e15) return false;
    quote.timestamp=uint32_t(timestamp/1000000);quote.timestampMs=uint64_t(timestamp/1000);
  } else if (source==4) {
    if (!channel || !root.is<JsonArrayConst>() || root.size()<3 ||
        root[0].as<uint32_t>()!=channel || !equals(root[1],"te") ||
        !root[2].is<JsonArrayConst>() || root[2].size()<4) return false;
    value=root[2][3];double timestamp=0;
    if (!number(root[2][1],timestamp) || timestamp<1 || timestamp>4.294967295e12) return false;
    quote.timestamp=uint32_t(timestamp/1000);quote.timestampMs=uint64_t(timestamp);
  } else if(source==3) {
    // Gemini's public trade-only subscription has no event-type field.
    // Nanosecond integers must not pass through floating point conversion.
    if(!equals(root["s"],"btcusd")||!root["e"].isNull()||
      !root["t"].is<uint64_t>()||!root["t"].as<uint64_t>()||!root["E"].is<uint64_t>())return false;
    uint64_t stamp=root["E"].as<uint64_t>();
    if(!stamp||stamp/1000000000ULL>UINT32_MAX)return false;
    value=root["p"];quote.timestamp=uint32_t(stamp/1000000000ULL);quote.timestampMs=stamp/1000000ULL;
  } else if(source==5) {
    if(!equals(root["e"],"aggTrade")||!equals(root["s"],"BTCUSDT")||
      !root["T"].is<uint64_t>()||!root["a"].is<uint64_t>()||!root["a"].as<uint64_t>())return false;
    uint64_t stamp=root["T"].as<uint64_t>();
    if(!stamp||stamp/1000>UINT32_MAX)return false;
    value=root["p"];quote.timestamp=uint32_t(stamp/1000);quote.timestampMs=stamp;
  } else return false;
  return number(value,quote.price) && validPrice(quote.price) && freshTimestamp(quote.timestamp,epoch);
}
}
