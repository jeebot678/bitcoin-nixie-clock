#pragma once
#include "MarketParsers.h"

namespace btc {
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
