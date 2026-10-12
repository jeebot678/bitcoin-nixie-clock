#pragma once
#include "MarketParsers.h"

namespace btc {
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
  } else return false;
  return number(value,quote.price) && validPrice(quote.price) && freshTimestamp(quote.timestamp,epoch);
}
}
