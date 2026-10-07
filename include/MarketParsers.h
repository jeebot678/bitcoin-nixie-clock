#pragma once
#include <ArduinoJson.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "ClockCore.h"

namespace btc {
struct Quote {
  double price = 0, usdtUsd = 0;
  uint32_t timestamp = 0; // Zero means endpoint provides no price timestamp.
};
inline bool number(JsonVariantConst value, double& result) {
  if (value.is<const char*>()) {
    const char* string = value.as<const char*>();
    char* end = nullptr;
    result = std::strtod(string, &end);
    if (end == string || !end || *end) return false;
  } else if (value.is<double>()) result = value.as<double>();
  else return false;
  return std::isfinite(result);
}
inline bool equals(JsonVariantConst value, const char* expected) {
  const char* string = value.as<const char*>();
  return string && std::strcmp(string, expected) == 0;
}
inline uint32_t epoch(int year, int month, int day, int hour, int minute, int second) {
  static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (year < 2024 || year > 2105 || month < 1 || month > 12 || day < 1 || day > days[month-1] + (month == 2 && leap) || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return 0;
  // Gregorian civil date to seconds since 1970, independent of local TZ.
  year -= month <= 2;
  const int era = year / 400;
  const unsigned yoe = unsigned(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t seconds = int64_t(era * 146097 + int(doe) - 719468) * 86400 + hour * 3600 + minute * 60 + second;
  return seconds > 0 && seconds <= UINT32_MAX ? uint32_t(seconds) : 0;
}
inline uint32_t isoTimestamp(const char* string) {
  if (!string || strlen(string) < 20) return 0;
  int y,m,d,h,n,s, consumed = 0;
  if (std::sscanf(string, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y,&m,&d,&h,&n,&s,&consumed) != 6 || consumed != 19) return 0;
  const char* suffix = string + consumed;
  if (*suffix == '.') { ++suffix; if (*suffix < '0' || *suffix > '9') return 0; while (*suffix >= '0' && *suffix <= '9') ++suffix; }
  if (strcmp(suffix, "Z")) return 0;
  return epoch(y,m,d,h,n,s);
}
inline uint32_t numericTimestamp(JsonVariantConst value, bool millis = false) {
  double result;
  if (!number(value, result) || result < 0 || floor(result) != result) return 0;
  if (millis) result = floor(result / 1000);
  return result >= config::kMinimumEpoch && result <= UINT32_MAX ? uint32_t(result) : 0;
}
inline uint32_t httpDate(const char* value) {
  int day,year,hour,minute,second; char month[4], zone[4];
  if (!value || std::sscanf(value, "%*3s, %d %3s %d %d:%d:%d %3s", &day,month,&year,&hour,&minute,&second,zone) != 7 || strcmp(zone,"GMT")) return 0;
  const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  for (int i=0;i<12;++i) if (!strcmp(month,months[i])) return epoch(year,i+1,day,hour,minute,second);
  return 0;
}
inline uint32_t retryAfter(const char* value, uint32_t now) {
  if (!value || !*value) return 0;
  char* end = nullptr; unsigned long seconds = strtoul(value,&end,10);
  if (end != value && !*end) return uint32_t(std::min<uint64_t>(86400,seconds)) * 1000;
  uint32_t date = httpDate(value);
  return date > now ? std::min<uint32_t>(date-now,86400) * 1000 : 0;
}
inline bool parseQuote(Provider provider, JsonVariantConst root, uint32_t now, Quote& quote) {
  quote = {};
  JsonVariantConst value;
  bool hasTimestamp = false;
  switch (provider) {
    case Provider::Coinbase:
      value = root["price"]; quote.timestamp = isoTimestamp(root["time"]); hasTimestamp = true; break;
    case Provider::Kraken:
      if (!root["error"].is<JsonArrayConst>() || root["error"].size()) return false;
      value = root["result"]["XXBTZUSD"]["c"][0];
      number(root["result"]["USDTZUSD"]["c"][0], quote.usdtUsd); break;
    case Provider::Bitstamp:
      value = root["last"]; quote.timestamp = numericTimestamp(root["timestamp"]); hasTimestamp = true; break;
    case Provider::Gemini:
      if (!equals(root["symbol"], "BTCUSD")) return false;
      value = root["close"]; break;
    case Provider::Bitfinex:
      if (!root.is<JsonArrayConst>() || root.size() < 10) return false;
      value = root[6]; break; // Extra fields are not documented price timestamps.
    case Provider::Binance:
    case Provider::Mexc:
      if (!equals(root["symbol"], "BTCUSDT")) return false;
      value = root["price"]; break;
    case Provider::Okx:
      if (!equals(root["code"],"0") || root["data"].size() != 1 || !equals(root["data"][0]["instId"],"BTC-USDT") || !equals(root["data"][0]["instType"],"SPOT")) return false;
      value = root["data"][0]["last"]; quote.timestamp = numericTimestamp(root["data"][0]["ts"],true); hasTimestamp = true; break;
    case Provider::Kucoin:
      if (!equals(root["code"],"200000")) return false;
      value = root["data"]["price"]; quote.timestamp = numericTimestamp(root["data"]["time"],true); hasTimestamp = true; break;
    case Provider::Gate:
      if (root.size() != 1 || !equals(root[0]["currency_pair"],"BTC_USDT")) return false;
      value = root[0]["last"]; break;
    case Provider::Bitget:
      if (!equals(root["code"],"00000") || root["data"].size() != 1 || !equals(root["data"][0]["symbol"],"BTCUSDT")) return false;
      value = root["data"][0]["lastPr"]; quote.timestamp = numericTimestamp(root["data"][0]["ts"],true); hasTimestamp = true; break;
  }
  return number(value, quote.price) && validPrice(quote.price) && (!hasTimestamp || freshTimestamp(quote.timestamp, now));
}
inline void historyFilter(JsonDocument& filter, bool bitstamp) {
  if (bitstamp) {
    filter["data"]["pair"] = true;
    filter["data"]["ohlc"][0]["timestamp"] = true;
    filter["data"]["ohlc"][0]["close"] = true;
  } else {
    filter["error"] = true;
    filter["result"]["XXBTZUSD"][0][0] = true;
    filter["result"]["XXBTZUSD"][0][4] = true;
  }
}
inline bool parseHistory(JsonVariantConst root, bool bitstamp, uint8_t window, uint32_t now, History& history) {
  history.count = 0;
  if (window >= 5 || now < config::kWindowSeconds[window]) return false;
  if (!bitstamp && (!root["error"].is<JsonArrayConst>() || root["error"].size())) return false;
  if (bitstamp && !equals(root["data"]["pair"], "BTC/USD")) return false;
  JsonArrayConst rows = bitstamp ? root["data"]["ohlc"].as<JsonArrayConst>() : root["result"]["XXBTZUSD"].as<JsonArrayConst>();
  if (rows.isNull() || rows.size() > 720) return false;
  uint32_t lastStart = 0;
  const uint32_t interval = config::kCandleSeconds[window];
  for (JsonVariantConst row : rows) {
    uint32_t start = numericTimestamp(bitstamp ? row["timestamp"] : row[0]);
    double price;
    if (!start || start % interval || (lastStart && start <= lastStart) || !number(bitstamp ? row["close"] : row[4],price) || !validPrice(price)) return false;
    lastStart = start;
    uint64_t close = uint64_t(start) + interval;
    if (close > now + uint64_t(interval)) return false;
    if (close > now || close < now - config::kWindowSeconds[window]) continue;
    if (!history.add(uint32_t(close),price)) return false;
  }
  return history.count >= 2 && history.samples[0].timestamp <= now - config::kWindowSeconds[window] + interval * 2 && history.samples[history.count-1].timestamp + interval * 2 >= now;
}
}
