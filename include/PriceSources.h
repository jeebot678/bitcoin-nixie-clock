#pragma once
#include <stddef.h>
#include <stdint.h>

namespace btc {
enum class Provider : uint8_t { Coinbase, Kraken, Bitstamp, Gemini, Bitfinex,
                              Binance, Okx, Kucoin, Gate, Bitget, Mexc };
struct PriceSource {
  Provider provider;
  const char* name;
  const char* url;
  bool usdt;
  uint32_t minimumIntervalMs;
};
// Eleven independent providers; public endpoints, no API keys/subscriptions.
// Full endpoint, quota, freshness and verification notes: docs/SOURCES.md.
constexpr PriceSource kSources[] = {
  {Provider::Coinbase, "Coinbase", "https://api.exchange.coinbase.com/products/BTC-USD/ticker", false, 5000},
  {Provider::Kraken, "Kraken", "https://api.kraken.com/0/public/Ticker?pair=XBTUSD,USDTUSD", false, 5000},
  {Provider::Bitstamp, "Bitstamp", "https://www.bitstamp.net/api/v2/ticker/btcusd/", false, 5000},
  {Provider::Gemini, "Gemini", "https://api.gemini.com/v2/ticker/btcusd", false, 5000},
  {Provider::Bitfinex, "Bitfinex", "https://api-pub.bitfinex.com/v2/ticker/tBTCUSD", false, 5000},
  {Provider::Binance, "Binance", "https://data-api.binance.vision/api/v3/ticker/price?symbol=BTCUSDT", true, 5000},
  {Provider::Okx, "OKX", "https://www.okx.com/api/v5/market/ticker?instId=BTC-USDT", true, 5000},
  {Provider::Kucoin, "KuCoin", "https://api.kucoin.com/api/v1/market/orderbook/level1?symbol=BTC-USDT", true, 5000},
  {Provider::Gate, "Gate", "https://api.gateio.ws/api/v4/spot/tickers?currency_pair=BTC_USDT", true, 5000},
  {Provider::Bitget, "Bitget", "https://api.bitget.com/api/v2/spot/market/tickers?symbol=BTCUSDT", true, 5000},
  {Provider::Mexc, "MEXC", "https://api.mexc.com/api/v3/ticker/price?symbol=BTCUSDT", true, 5000},
};
constexpr size_t kSourceCount = sizeof(kSources) / sizeof(kSources[0]);
}
