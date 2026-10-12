#pragma once
#include <ArduinoJson.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "HistoryCache.h"
#include "CandleChart.h"

namespace btc {
struct Quote {
  double price = 0, usdtUsd = 0;
  uint32_t timestamp = 0; // Zero means endpoint provides no price timestamp.
  uint64_t timestampMs = 0;
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
inline uint64_t isoTimestampMs(const char* value){
  uint32_t seconds=isoTimestamp(value);if(!seconds)return 0;
  unsigned fraction=0;if(value[19]=='.'){const char* p=value+20;for(unsigned i=0;i<3;++i){fraction*=10;if(*p>='0'&&*p<='9')fraction+=unsigned(*p++-'0');}}
  return uint64_t(seconds)*1000+fraction;
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
      value = root["price"]; quote.timestamp = isoTimestamp(root["time"]); quote.timestampMs=isoTimestampMs(root["time"]); hasTimestamp = true; break;
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
  if(!quote.timestampMs&&quote.timestamp)quote.timestampMs=uint64_t(quote.timestamp)*1000;
  return number(value, quote.price) && validPrice(quote.price) && (!hasTimestamp || freshTimestamp(quote.timestamp, now));
}
inline void quoteFilter(JsonDocument& filter,Provider provider){
  if(provider==Provider::Bitfinex){filter.set(true);return;}
  if(provider==Provider::Gate){filter[0]["currency_pair"]=true;filter[0]["last"]=true;return;}
  filter["code"]=true;
  switch(provider){
    case Provider::Coinbase:filter["price"]=true;filter["time"]=true;break;
    case Provider::Kraken:filter["error"]=true;filter["result"]["XXBTZUSD"]["c"]=true;filter["result"]["USDTZUSD"]["c"]=true;break;
    case Provider::Bitstamp:filter["last"]=true;filter["timestamp"]=true;break;
    case Provider::Gemini:filter["symbol"]=true;filter["close"]=true;break;
    case Provider::Binance:case Provider::Mexc:filter["symbol"]=true;filter["price"]=true;break;
    case Provider::Okx:filter["data"][0]["instId"]=true;filter["data"][0]["instType"]=true;filter["data"][0]["last"]=true;filter["data"][0]["ts"]=true;break;
    case Provider::Kucoin:filter["data"]["price"]=true;filter["data"]["time"]=true;break;
    case Provider::Bitget:filter["data"][0]["symbol"]=true;filter["data"][0]["lastPr"]=true;filter["data"][0]["ts"]=true;break;
    default:break;
  }
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
// Stream one trade object at a time: a whole minute of trades never requires
// a whole-response JSON allocation. Keep the newest trade ID per real second.
template<class Reader>
bool parseRecentTrades(Reader& reader,JsonDocument& document,bool bitstamp,uint8_t window,uint32_t now,HistoryView& history){
  history.count=0;if(window>=2)return false;
  auto next=[&](){int c;do{c=reader.read();}while(c==' '||c=='\r'||c=='\n'||c=='\t');return c;};
  if(next()!='[')return false;
  StaticJsonDocument<256> filter;filter["price"]=true;filter[bitstamp?"date":"time"]=true;filter[bitstamp?"tid":"trade_id"]=true;
  uint64_t ids[62]={};unsigned rows=0;int c=next();
  if(c==']')return next()==-1;
  for(;;){
    if(c!='{'||++rows>4096)return false;
    struct PrefixReader{Reader& source;bool first=true;explicit PrefixReader(Reader& r):source(r){}int read(){if(first){first=false;return '{';}return source.read();}size_t readBytes(char* out,size_t n){size_t i=0;for(;i<n;++i){int value=read();if(value<0)break;out[i]=char(value);}return i;}} item(reader);
    document.clear();if(deserializeJson(document,item,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(6)))return false;
    auto root=document.template as<JsonVariantConst>();double price,id;
    uint32_t stamp=bitstamp?numericTimestamp(root["date"]):isoTimestamp(root["time"]);
    if(!stamp||!number(root["price"],price)||!validPrice(price)||!number(root[bitstamp?"tid":"trade_id"],id)||id<1||id>9007199254740991.0||std::floor(id)!=id||stamp>uint64_t(now)+10)return false;
    if(stamp>=now-config::kWindowSeconds[window]&&stamp<=now){
      size_t at=0;while(at<history.count&&history.samples[at].timestamp<stamp)++at;
      if(at<history.count&&history.samples[at].timestamp==stamp){if(uint64_t(id)>ids[at]){history.samples[at].price=price;ids[at]=uint64_t(id);}}
      else{if(history.count>=std::min<unsigned>(62,history.capacity))return false;memmove(history.samples+at+1,history.samples+at,(history.count-at)*sizeof(Sample));memmove(ids+at+1,ids+at,(history.count-at)*sizeof(uint64_t));history.samples[at]={stamp,price};ids[at]=uint64_t(id);++history.count;}
    }
    c=next();if(c==']')return next()==-1;if(c!=',')return false;c=next();
  }
}
// Keep every trade high/low while streaming a bounded object pool. Trade IDs
// disambiguate open/close when Bitstamp exposes only whole-second timestamps.
template<class Reader,class Yield>
bool parseTradeCandles(Reader& reader,JsonDocument& document,bool bitstamp,uint8_t window,uint64_t nowMs,CandleSeries& candles,Quote& latest,Yield cooperate){
  candles={};latest={};if(window>=5||nowMs<uint64_t(config::kMinimumEpoch)*1000)return false;
  auto next=[&](){int c;do{c=reader.read();}while(c==' '||c=='\r'||c=='\n'||c=='\t');return c;};
  if(next()!='[')return false;
  StaticJsonDocument<256> filter;filter["price"]=true;filter[bitstamp?"date":"time"]=true;filter[bitstamp?"tid":"trade_id"]=true;
  uint64_t openId[21]={},closeId[21]={},latestId=0;uint32_t rows=0;int c=next();
  if(c==']')return next()==-1;
  for(;;){
    if(c!='{'||++rows>50000)return false;
    struct PrefixReader{Reader& source;bool first=true;explicit PrefixReader(Reader& r):source(r){}int read(){if(first){first=false;return '{';}return source.read();}size_t readBytes(char* out,size_t n){size_t i=0;for(;i<n;++i){int value=read();if(value<0)break;out[i]=char(value);}return i;}} item(reader);
    document.clear();if(deserializeJson(document,item,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(6)))return false;
    auto root=document.template as<JsonVariantConst>();double price,id;
    uint64_t stamp=bitstamp?uint64_t(numericTimestamp(root["date"]))*1000:isoTimestampMs(root["time"]);
    if(!stamp||!number(root["price"],price)||!validPrice(price)||price>UINT32_MAX/100.0||!number(root[bitstamp?"tid":"trade_id"],id)||id<1||id>9007199254740991.0||std::floor(id)!=id||stamp>nowMs+10000)return false;
    if(stamp<=nowMs){
      uint32_t bucket=candleBucket(stamp,window),current=candleBucket(nowMs,window);unsigned slot=bucket%21;
      if(bucket+20>=current){
        auto before=candles.bars[slot];uint32_t offset=uint32_t(stamp-candleStart(bucket,window));
        bool openTie=!openId[slot]||uint64_t(id)<openId[slot],closeTie=uint64_t(id)>closeId[slot];
        candles.add(stamp,price,window,nowMs,openTie,closeTie);
        if(before.bucket!=bucket||offset<before.openAt||(offset==before.openAt&&openTie))openId[slot]=uint64_t(id);
        if(before.bucket!=bucket||offset>before.closeAt||(offset==before.closeAt&&closeTie))closeId[slot]=uint64_t(id);
      }
      if(stamp>latest.timestampMs||(stamp==latest.timestampMs&&uint64_t(id)>latestId)){
        latest.price=price;latest.timestamp=uint32_t(stamp/1000);latest.timestampMs=stamp;latestId=uint64_t(id);
      }
    }
    if(rows%64==0)cooperate();
    c=next();if(c==']')return next()==-1;if(c!=',')return false;c=next();
  }
}
inline uint32_t historyRequestStart(uint8_t window, uint32_t epoch, uint32_t start = 0) {
  uint32_t first = firstHistoryClose(window, epoch);
  if (!first) return 0;
  uint32_t interval = config::kCandleSeconds[window], oldest = first - interval;
  return start && start >= oldest && start % interval == 0 && start < epoch / interval * interval ? start : oldest;
}
inline size_t historyCapacity(bool bitstamp, uint8_t window, uint32_t epoch = 0, uint32_t start = 0) {
  if (window>=5) return 0;
  size_t rows=config::kWindowSeconds[window]/config::kCandleSeconds[window]+4;
  if (epoch && start) rows = std::min<size_t>(rows, (epoch - historyRequestStart(window, epoch, start)) / config::kCandleSeconds[window] + 3);
  // ArduinoJson 6 array filters retain the entire numeric Kraken candle.
  // Reserve per-row slots and copied strings, with room for metadata/errors.
  return 2048+rows*(bitstamp ? JSON_ARRAY_SIZE(1)+JSON_OBJECT_SIZE(2)+48
                                   : JSON_ARRAY_SIZE(9)+64);
}
inline bool parseHistory(JsonVariantConst root, bool bitstamp, uint8_t window, uint32_t now, HistoryView& history, uint32_t start = 0) {
  history.count = 0;
  if (window >= 5 || now < config::kWindowSeconds[window]) return false;
  if (!bitstamp && (!root["error"].is<JsonArrayConst>() || root["error"].size())) return false;
  if (bitstamp && !equals(root["data"]["pair"], "BTC/USD")) return false;
  JsonArrayConst rows = bitstamp ? root["data"]["ohlc"].as<JsonArrayConst>() : root["result"]["XXBTZUSD"].as<JsonArrayConst>();
  if (rows.isNull() || rows.size() > 720) return false;
  uint32_t lastStart = 0;
  const uint32_t interval = config::kCandleSeconds[window];
  const uint32_t first = historyRequestStart(window, now, start) + interval;
  for (JsonVariantConst row : rows) {
    uint32_t start = numericTimestamp(bitstamp ? row["timestamp"] : row[0]);
    double price;
    if (!start || start % interval || (lastStart && start <= lastStart) || !number(bitstamp ? row["close"] : row[4],price) || !validPrice(price)) return false;
    lastStart = start;
    uint64_t close = uint64_t(start) + interval;
    if (close > now + uint64_t(interval)) return false;
    if (close > now || close < first) continue;
    if (!history.add(uint32_t(close),price)) return false;
  }
  return history.count >= 1 && history.samples[0].timestamp == first &&
    history.samples[history.count-1].timestamp == now / interval * interval;
}
}
