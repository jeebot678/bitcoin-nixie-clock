# Free Bitcoin market data sources

Verified on 2026-10-10 using HTTP/1.0, identity encoding and the same Mozilla roots embedded in the firmware: all eleven default tickers returned HTTP 200 and parsed as valid prices. Kraken and Bitstamp returned valid history for all five windows and actual incremental requests for one closed hour and 48 closed hours. Bybit returned HTTP 403 from this network and is excluded. The exact responses, timings and headers are in `test/live_probe_report.json` and `test/live_fixtures/`.

These exchanges can report different last-trade prices. Firmware retains up to
twelve real whole-dollar alternatives per provider for five seconds and prefers
a quote that changes multiple displayed digits. Repeated receipts refresh an
entry rather than erasing other prices. The Nixie deadline runs independently
of fetching; a selected cached quote is a recently observed market price, not a
claim that another trade occurred at that deadline. REST requests use at least
five seconds per provider, including shared history/FX traffic. No API keys or
subscriptions are required. See [exact refresh behavior](NIXIE_REFRESH.md).

## Device live streams

Firmware 1.0.20 prefers Coinbase on the primary verified TLS connection and
Kraken on the secondary. Binance, Bitstamp, Bitfinex and Gemini provide
fallbacks; only two connections run concurrently. A provider can belong to
only one worker. Each worker retains a nonblocking 16-tick FIFO so a batch of
real prices reaches the quote selector and candle builder. Overflow discards
the oldest tick and increments telemetry rather than blocking network reads.

Dispatching an ordinary HTTP job holds new handshakes without closing
established subscriptions. The secondary releases memory only when the actual
data-RAM reservation needs it. Quote/FX requests defer rather than close the
primary; OTA and an infrequent candle bootstrap can pause it when necessary.
Missing feeds get a bounded 35-second reconnect grace, including recovery of
a second feed while the first remains healthy. Retries back off 5–60 seconds,
at least a minute for HTTP 429, and an hour for authorization/geographic errors.
The TLS gate and conservative memory budget remain enabled.

| Provider | Endpoint / public subscription | Rules and freshness |
| --- | --- | --- |
| Coinbase | `wss://ws-feed.exchange.coinbase.com`; `ticker` and `heartbeat`, `BTC-USD` | Primary USD feed. Ticker on matches, batching cascades; actual trade timestamp. Five-second `ticker_batch` is excluded. The current limits page lists 8 requests/s/IP, burst 20, and a 10 RPS / 1,000 burst inbound-message limit; firmware sends one subscription and infrequent heartbeat frames. [Channels](https://docs.cdp.coinbase.com/exchange/websocket-feed/channels), [limits](https://docs.cdp.coinbase.com/exchange/websocket-feed/rate-limits). |
| Kraken | `wss://ws.kraken.com/v2`; `ticker` for `BTC/USD` and `USDT/USD`, `event_trigger:bbo`, snapshot enabled; separate `trade` subscription for `BTC/USD`, snapshot disabled | Both subscriptions share one TLS connection. BBO ticker updates can repeat the last price; matched-trade batches retain validated rows and fence duplicate trade IDs. FX ticks update conversion without counting as Bitcoin prices or making an inactive Bitcoin feed look fresh. About 150 connection attempts/rolling 10 minutes/IP; reconnect no faster than five seconds after downtime. [Ticker](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/ticker), [trades](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/trade), [connection rules](https://docs.kraken.com/exchange/guides/websockets/introduction). |
| Binance | `wss://data-stream.binance.vision/ws/btcusdt@aggTrade`; raw market-data-only stream, no subscription message needed | Real aggregate execution price and millisecond trade time; exact `BTCUSDT` symbol and increasing aggregate ID required. Eligible only with fresh USDT/USD conversion. Five incoming control/ping/pong messages/s, 300 connection attempts/5 min/IP, at most 1,024 streams/connection. The client uses one stream and infrequent heartbeat frames. [Official stream and quota documentation](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md). |
| Bitstamp | `wss://ws.bitstamp.net`; `live_trades_btcusd` | Real USD trade price, microtimestamp and increasing ID; honor reconnect events. No numeric public subscription quota established from retrieved docs; one subscription and conservative retries. [Official documentation](https://www.bitstamp.net/websocket/v2/). |
| Bitfinex | `wss://api-pub.bitfinex.com/ws/2`; `trades`, `tBTCUSD` | 30 public subscriptions/connection. Bind the acknowledged BTC/USD channel, accept `te` with execution milliseconds, ignore `tu`, snapshots, heartbeats and repeated trade IDs. [Trades](https://docs.bitfinex.com/reference/ws-public-trades), [general rules](https://docs.bitfinex.com/docs/ws-general). |
| Gemini | `wss://ws.gemini.com`; `{"id":"1","method":"subscribe","params":["btcusd@trade"]}` | Public USD trade-only stream; exact instrument, execution nanoseconds and increasing trade ID required. Integer timestamp parsing preserves millisecond precision. The verified connection's `conninfo` advertised request weight 7,000/10 s and 300 connection attempts/5 min; these are observed live allowances, not a permanent promise. One subscription and conservative retries remain far below them. JSON status 429 receives at least a minute of backoff. [Production endpoint](https://developer.gemini.com/trading/websocket/introduction), [trade schema](https://developer.gemini.com/trading/websocket/streams), [connection-info method](https://developer.gemini.com/trading/websocket/playground). |

All six feeds were reached by computer probes. The 2026-10-13 added-feed
probe received 279 Binance trades and one Gemini trade in 30 seconds and
saved the exact payloads and Gemini live quotas in
[the stream recovery report](../test/stream_recovery_probe_report.json).
Separate TLS handshakes for both hosts passed with the embedded Mozilla roots
using TLS 1.2 / ECDHE-RSA-AES128-GCM-SHA256. Sparse exchanges remain useful
fallbacks; reception volume is measured rather than inferred from a timer.

A separate 60-second computer test used only Coinbase USD ticker and Kraken
USD BBO ticker. Its JavaScript replica of the firmware quote selector kept
12 distinct prices per provider with a five-second receipt lifetime. After
five seconds of warmup, 547 display samples contained 546 changes across all
546 transitions, including 516 multi-digit changes, no empty frames and
99–102 ms timer gaps. It received 325 Coinbase and 832 Kraken prices; no
synthetic adjustments were made. This demonstrates the pair's behavior during
that observation, not ESP32 transport performance or a guaranteed new trade
every 100 ms. See [the computer report](../test/usd_pair_frames_report.json)
and [separate device measurements](NIXIE_REFRESH.md).

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

Kraken's verified `USDT/USD` ticker maintains conversion over the existing live socket. The REST fallback checks conversion separately from the Nixie dial, at a 60-second target when stream recovery needs it. Rates expire 120 seconds after receipt. Without a fresh rate, all USDT providers are skipped and the five USD providers remain eligible; an active Binance socket closes so the worker can try a USD fallback. FX-only ticks do not update the displayed Bitcoin price or reset Bitcoin receipt-age telemetry. Depegs are converted rather than treated as $1; rates outside 0.5–1.5 USD are rejected.

HTTP 429/418 and recognized JSON rate-limit errors produce at least a one-minute cooldown; `Retry-After` seconds or HTTP date extends it. Authentication, geographic and missing-endpoint errors (401/403/404/451) cool down for an hour. Other failures back off from 5 seconds to 10 minutes. Prices, identifiers and numeric strings must parse correctly; malformed JSON, missing data, NaN/infinity, compressed/chunked unexpected bodies and overlarge responses are rejected. Requests use bounded read/connect/handshake times and certificate verification, never `setInsecure()`.

## Candle history

Firmware 1.0.18 builds exact range/21 OHLC candles from individual trades.
One-minute exchange bars cannot supply the correct high/low for shorter,
non-aligned column intervals.

Bitstamp primary uses `https://www.bitstamp.net/api/v2/transactions/btcusd/?time=minute`
for 30 s / 1 min, and `?time=hour` for 5 min / 30 min / 1 h.
The [official API](https://www.bitstamp.net/api/) documents these time windows
and the public quotas listed above. Coinbase fallback uses
`https://api.exchange.coinbase.com/products/BTC-USD/trades?limit=1000`;
[the product-trades documentation](https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/products/get-product-trades)
documents pagination and a default limit of 1,000. That page can cover less
than the requested range. Coverage metadata distinguishes partial history
from a complete time window; display-only close holds cover unobserved
columns without adding invented trades to the OHLC cache.

Both were verified on 2026-10-12 in HTTP/1.0 with the embedded Mozilla roots.
Bitstamp returned 3,513 trades / 358,326 bytes for the hour; Coinbase returned
1,000 trades / 119,428 bytes. The exact fixtures, timing, framing and
independently calculated OHLC references are in
[the candle-trades probe report](../test/candle_trades_probe_report.json).

A 512-byte object pool streams each response into separate 21-record OHLC
series for every attempted range. Minute responses fill both short ranges;
hour / Coinbase responses can fill all five. The
parser validates finite prices, timestamps, IDs and complete response framing,
with bounds of 50,000 trades, 4 MiB and a 20-second body deadline. It yields
to the worker scheduler every 64 trades. UTC milliseconds and trade IDs
preserve chronological opens/closes, including Bitstamp's second-level dates.
Prices are genuine USD; no present-day FX rate is applied to old USD history.
These calls share each provider's quote/history quota, use at least five
seconds between requests, and normal checks are spaced at one minute.
Live updates maintain all five caches even while a different range is selected.
Healthy streams suppress completed bootstrap downloads. History traffic
does not follow the 100 ms tube timer. See [candle storage and scale](CANDLE_CHART.md).

Kraken `/0/public/OHLC` and Bitstamp `/api/v2/ohlc/btcusd/` parsing remains
covered by the legacy cache compatibility tests, but the live candle chart
uses individual trades rather than close-only history.

## OTA endpoint

The updater reads `https://raw.githubusercontent.com/jeebot678/bitcoin-nixie-clock/main/ota/manifest.json` and accepts firmware only from the same repository's signed, versioned release URL. These public downloads need no GitHub token on the device. GitHub-owned HTTPS redirects are allowlisted. The manifest signature also binds the binary URL, hash, size, version and board. See [GitHub releases documentation](https://docs.github.com/en/rest/releases/releases) and [Espressif OTA/rollback documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html).

## Initial short-range endpoint check

The earlier minute-transactions / 100-trades check and exact fixtures remain in
[the recent-trades probe report](../test/recent_trades_probe_report.json).
The live candle chart now uses the routes and 1,000-trade fallback described above.
