# Free Bitcoin market data sources

Verified on 2026-10-10 using HTTP/1.0, identity encoding and the same Mozilla roots embedded in the firmware: all eleven default tickers returned HTTP 200 and parsed as valid prices. Kraken and Bitstamp returned valid history for all five windows and actual incremental requests for one closed hour and 48 closed hours. Bybit returned HTTP 403 from this network and is excluded. The exact responses, timings and headers are in `test/live_probe_report.json` and `test/live_fixtures/`.

These exchanges can report different last-trade prices. Firmware 1.0.9 caches accepted quotes per provider and prefers recent quotes that change whole-dollar digits. REST uses at least five seconds per provider, including shared history/FX traffic, and respects quotas and geographic restrictions. Version 1.0.16 removes the cosmetic rounding effect, retains three distinct real prices per source for five seconds, and prefers candidates that change multiple digits. Matching prices trigger an asynchronous request to another source. Charts/history retain real values. See [exact refresh behavior](NIXIE_REFRESH.md).

## Device live streams

Two independent verified TLS connections prefer Kraken and Coinbase, with Bitstamp and Bitfinex as fallback. Each worker owns its queue; a busy feed cannot overwrite another's waiting quote. One provider cannot be owned by both workers. The secondary releases memory during HTTP work. Ordinary requests defer rather than close the primary if RAM is insufficient; a routine OTA reservation may pause it. Retries back off 5–60 seconds, at least a minute for HTTP 429, and an hour for authorization/geographic errors.

| Provider | Endpoint / public subscription | Rules and freshness |
| --- | --- | --- |
| Kraken | `wss://ws.kraken.com/v2`; `ticker`, `BTC/USD`, `event_trigger:bbo`, snapshot enabled | Last-trade price, UTC ticker timestamp. BBO updates can repeat a price. About 150 connection attempts/rolling 10 minutes/IP; reconnect no faster than five seconds after downtime. [Ticker](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/ticker), [connection rules](https://docs.kraken.com/exchange/guides/websockets/introduction). |
| Coinbase | `wss://ws-feed.exchange.coinbase.com`; `ticker` and `heartbeat`, `BTC-USD` | Ticker on matches, batching cascades; actual trade timestamp. Five-second `ticker_batch` is excluded. Client messages 10/s, burst 1,000. [Channels](https://docs.cdp.coinbase.com/exchange/websocket-feed/channels), [limits](https://docs.cdp.coinbase.com/exchange/websocket-feed/rate-limits). |
| Bitstamp | `wss://ws.bitstamp.net`; `live_trades_btcusd` | Real trade price, microtimestamp and ID; honor reconnect events. No numeric public subscription quota established from retrieved docs; one subscription and conservative retries. [Official documentation](https://www.bitstamp.net/websocket/v2/). |
| Bitfinex | `wss://api-pub.bitfinex.com/ws/2`; `trades`, `tBTCUSD` | 30 public subscriptions/connection. Bind the acknowledged BTC/USD channel, accept `te` with execution milliseconds, ignore `tu`, snapshots, heartbeats and repeated trade IDs. [Trades](https://docs.bitfinex.com/reference/ws-public-trades), [general rules](https://docs.bitfinex.com/docs/ws-general). |

All four were reached by the computer probe. Physical ESP32 reception is measured separately. Exchange events determine quote arrival; the display timer does not imply a new trade every 500 ms. Selection, expiry and cosmetic rendering are documented in [NIXIE_REFRESH.md](NIXIE_REFRESH.md).

## Public REST endpoints

| Provider | Verified public endpoint | Official quota/rule | Parsed last price / freshness |
| --- | --- | --- | --- |
| Coinbase Exchange | `https://api.exchange.coinbase.com/products/BTC-USD/ticker` | 10 req/s/IP; burst 15. [Official limits](https://docs.cdp.coinbase.com/exchange/rest-api/rate-limits), [ticker](https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/products/get-product-ticker) | `price`, actual last-trade ISO `time`; reject over 120 s old. USD. |
| Kraken | `https://api.kraken.com/0/public/Ticker?pair=XBTUSD,USDTUSD` | Public requests at 1 req/s or less remain within limits; ticker IP limiter, OHLC IP + pair limiter. [Official public limits](https://support.kraken.com/gb/articles/206548367-what-are-the-api-rate-limits-), [ticker](https://docs.kraken.com/api-reference/market-data/get-ticker-information) | `result.XXBTZUSD.c[0]`; `USDTZUSD.c[0]` provides FX. No trade timestamp. USD. |
| Bitstamp | `https://www.bitstamp.net/api/v2/ticker/btcusd/` | 400 req/s and 10,000 req/10 min. [Official API](https://www.bitstamp.net/api/) | `last`, response `timestamp`. USD. |
| Gemini | `https://api.gemini.com/v2/ticker/btcusd` | Public 120 req/min; recommends ≤1 req/s. [Official limits](https://developer.gemini.com/rate-limit), [market data](https://developer.gemini.com/trading/rest-api/market-data) | `close`; require `symbol=BTCUSD`. No price timestamp. USD. |
| Bitfinex | `https://api-pub.bitfinex.com/v2/ticker/tBTCUSD` | This ticker: 90 req/min. [Official ticker schema and quota](https://docs.bitfinex.com/reference/rest-public-ticker) | Array index 6, LAST_PRICE; ≥10 fields required. Additional fields are ignored, not assumed to be timestamps. USD. |
| Binance market-data-only | `https://data-api.binance.vision/api/v3/ticker/price?symbol=BTCUSDT` | Single-symbol request weight 2/IP; live `exchangeInfo.rateLimits` verified a 6,000 weight/minute budget and 300,000 raw requests/5 minutes. Usage is in `X-MBX-USED-WEIGHT-*`; allowances can change. [Official market endpoint](https://developers.binance.com/en/docs/catalog/core-trading-spot-trading/api/rest-api/market), [public data base URL](https://developers.binance.com/en/docs/products/spot/rest-api) | `price`, require `symbol=BTCUSDT`; no trade timestamp. USDT → USD. |
| OKX | `https://www.okx.com/api/v5/market/ticker?instId=BTC-USDT` | 20 req/2 s/IP. [Official ticker API](https://app.okx.com/docs-v5/en#rest-api-market-data-get-ticker) | `data[0].last`, snapshot `ts`; require code 0, spot and correct instrument. USDT → USD. |
| KuCoin | `https://api.kucoin.com/api/v1/market/orderbook/level1?symbol=BTC-USDT` | Weight 2 from public pool of 2,000/30 s/IP. [Official ticker](https://www.kucoin.com/docs-new/rest/spot-trading/market-data/get-ticker), [pool limits](https://www.kucoin.com/docs-new/rate-limit-rule-classic) | `data.price`, `data.time`; require code 200000. USDT → USD. |
| Gate | `https://api.gateio.ws/api/v4/spot/tickers?currency_pair=BTC_USDT` | 200 req/10 s per endpoint/IP; verified response advertised limit 200, remaining 199. [Official API limits](https://www.gate.com/docs/developers/apiv4/en/#frequency-limit-rule), [spot ticker](https://www.gate.com/docs/developers/apiv4/en/spot/#retrieve-ticker-information) | Array `[0].last`, require exact pair. No price timestamp. USDT → USD. |
| Bitget | `https://api.bitget.com/api/v2/spot/market/tickers?symbol=BTCUSDT` | 20 req/s/IP. [Official classic spot market API](https://www.bitget.com/zh-CN/docs/catalog/classic-spot-market/classic-spot-market) | `data[0].lastPr`, `ts`; require code 00000 and symbol. USDT → USD. |
| MEXC | `https://api.mexc.com/api/v3/ticker/price?symbol=BTCUSDT` | Updated limits describe 100 single queries/s, weight 5, unauthenticated IP weight budget 500. Older documentation labels this endpoint weight 1; firmware conservatively stays at ≤0.2 req/s. [Updated official limits](https://www.mexc.com/en-GB/announcements/article/term-definitions-17827791529303), [endpoint](https://mexcdevelop.github.io/apidocs/spot_v3_en/#symbol-price-ticker) | `price`, require `symbol=BTCUSDT`; no trade timestamp. USDT → USD. |

## Freshness, validation and failures

Timestamp-free sources cannot prove how recently the underlying trade occurred. Firmware checks their TLS-verified response `Date` (within 20 seconds), rejects `Age` over 15 seconds and requests `Cache-Control: no-cache`; those checks detect old responses but cannot guarantee an exchange's internal publication latency. The five APIs with embedded timestamps are also validated for staleness/future timestamps. There is no documented hard, end-to-end latency guarantee for these free REST APIs. Web scraping introduces extra caching and format changes and is unnecessary here.

From firmware 1.0.10, the first fresh, validated source displays immediately. An independent provider agreeing within 1% confirms it in the background; before confirmation, changes within 1% update immediately, while larger moves still need two agreeing providers. After confirmation, moves within 5% update immediately; larger moves require independent 1% agreement within one minute. This is an outlier guard, not a global composite index. Prices remain subject to differences between exchanges. Firmware 1.0.9 blocked the first display until independent confirmation.

Kraken's USDT/USD rate refreshes every 60 seconds independently of the Nixie refresh dial and expires after 120 seconds. Without it, all USDT providers are skipped and the five USD providers remain eligible. FX-only refreshes do not update the displayed Bitcoin price. Depegs are converted rather than treated as $1; rates outside 0.5–1.5 USD are rejected.

HTTP 429/418 and recognized JSON rate-limit errors produce at least a one-minute cooldown; `Retry-After` seconds or HTTP date extends it. Authentication, geographic and missing-endpoint errors (401/403/404/451) cool down for an hour. Other failures back off from 5 seconds to 10 minutes. Prices, identifiers and numeric strings must parse correctly; malformed JSON, missing data, NaN/infinity, compressed/chunked unexpected bodies and overlarge responses are rejected. Requests use bounded read/connect/handshake times and certificate verification, never `setInsecure()`.

## Historical candles

Kraken fallback: `https://api.kraken.com/0/public/OHLC?pair=XBTUSD&interval=MINUTES&since=EPOCH`. Parse `result.XXBTZUSD`, close at index 4. [Official documentation](https://docs.kraken.com/api-reference/market-data/get-ohlc-data) limits the response to 720 rows and identifies the final candle as unfinished.

Bitstamp primary: `https://www.bitstamp.net/api/v2/ohlc/btcusd/?step=SECONDS&limit=MISSING_COUNT&end=LAST_CLOSED_SECOND&exclude_current_candle=true`. Firmware caps each page at 24 candles, advancing the end time from the first missing candle. Parse `data.ohlc[].close`; confirm pair BTC/USD. [Official documentation](https://www.bitstamp.net/api/) allows at most 1,000 candles. These are genuine USD history, so a current FX rate is never applied retrospectively to stablecoin history.

| Window | Candle interval | Full closed-candle count | Update trigger |
| --- | --- | --- | --- |
| 5 minutes | 60 s | 5–6 | New closed minute |
| 30 minutes | 60 s | 30–31 | New closed minute |
| 1 hour | 60 s | 60–61 | New closed minute |
| 24 hours | 300 s | 288–289 | New closed five-minute candle |
| 7 days | 3,600 s | 168–169 | New closed hour |

Only closed candles are plotted, timestamped at close. Rows are validated for order, duplicates, interval alignment, coverage, recency and finite positive prices. Live data overlays its actual timestamp bucket. No synthetic history or invented gap filling is used. Only the selected window fetches history; all caches persist in flash. The three short windows share incoming one-minute candles. Incremental requests begin at the first missing closed candle in the current range, including internal gaps. Kraken gets `since` with one-candle overlap; Bitstamp gets the missing count and an explicit closed end time. A new one-candle response is valid. Merging preserves existing points, replaces matching timestamps, inserts missing points in order and discards points outside the current range. The 5-minute range initially has five or six minute-resolution historical points. The graph connects these points at their actual candle spacing and includes five-second live samples retained since boot; live prices continue independently.

## OTA endpoint

The updater reads `https://raw.githubusercontent.com/jeebot678/bitcoin-nixie-clock/main/ota/manifest.json` and accepts firmware only from the same repository's signed, versioned release URL. These public downloads need no GitHub token on the device. GitHub-owned HTTPS redirects are allowlisted. The manifest signature also binds the binary URL, hash, size, version and board. See [GitHub releases documentation](https://docs.github.com/en/rest/releases/releases) and [Espressif OTA/rollback documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html).
