#pragma once
#include "MarketParsers.h"

namespace btc {
// Only authenticated TLS public BTC/USD messages are passed to this parser.
// A snapshot is a real last price, not a newly invented trade.
inline bool parseStreamQuote(uint8_t source, JsonVariantConst root, uint32_t epoch, Quote& quote) {
  quote={};
  JsonVariantConst value;
  if (source==0) {
    if (!equals(root["type"],"ticker") || !equals(root["product_id"],"BTC-USD")) return false;
    value=root["price"];quote.timestamp=isoTimestamp(root["time"]);
  } else if (source==1) {
    if (!equals(root["channel"],"ticker") ||
        (!equals(root["type"],"snapshot") && !equals(root["type"],"update")) ||
        !root["data"].is<JsonArrayConst>() || root["data"].size()!=1 ||
        !equals(root["data"][0]["symbol"],"BTC/USD")) return false;
    value=root["data"][0]["last"];quote.timestamp=isoTimestamp(root["data"][0]["timestamp"]);
  } else return false;
  return number(value,quote.price) && validPrice(quote.price) && freshTimestamp(quote.timestamp,epoch);
}
}
