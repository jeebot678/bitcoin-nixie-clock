# Free Bitcoin market data sources

Verified on 2026-10-10 using HTTP/1.0, identity encoding and the same Mozilla roots embedded in the firmware: all eleven default tickers returned HTTP 200 and parsed as valid prices. Kraken and Bitstamp returned valid history for all five windows and actual incremental requests for one closed hour and 48 closed hours. Bybit returned HTTP 403 from this network and is excluded. The exact responses, timings and headers are in `test/live_probe_report.json` and `test/live_fixtures/`.

These exchanges can report different last-trade prices. Firmware retains up to
twelve real whole-dollar alternatives per provider for five seconds and prefers
a quote that changes multiple displayed digits. Repeated receipts refresh an
entry rather than erasing other prices. Selection remembers the last eight
distinct displayed numbers, excludes a return to the previous distinct number
and ranks least-recent values before digit changes. Equal choices use fresh
entropy without favoring duplicate entries or a provider. Holding a number
does not erase that history. The Nixie deadline runs independently
of fetching; a selected cached quote is a recently observed market price, not a
claim that another trade occurred at that deadline. From firmware 1.0.24, the
latest BBO midpoint has the live-book exception described below; older book
alternatives and all last-trade quotes still expire after five seconds.
REST requests use at least
five seconds per provider, including shared history/FX traffic. No API keys or
paid plans are required. See [exact refresh behavior](NIXIE_REFRESH.md).

## Device live streams

Firmware 1.0.25 prefers Coinbase on the primary verified TLS connection and
Bitfinex depth-one books on the secondary. Direct BTC/USD and an implied
BTC/USDT × USDT/USD midpoint provide separate real USD display candidates.
All three Bitfinex books share the same socket. Kraken, Binance, Bitstamp and
Gemini provide fallbacks; only two connections run concurrently. A provider can belong to
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
| Bitfinex | `wss://api-pub.bitfinex.com/ws/2`; `conf` flag `32768`, then three `book` subscriptions for `tBTCUSD`, `tBTCUST`, `tUSTUSD`, each `prec:P0`, `freq:F0`, `len:"1"` | Current direct USD midpoint and USD-converted midpoint from two current books. Publication timestamp is appended outside the book array. Bind each exact instrument, precision, frequency, length and distinct channel, reconstruct snapshots/deletions, reject crossed or excessively wide quotes, stale/future/backward publication times and malformed rows. Lost/malformed frames require a fresh snapshot through reconnect. Heartbeats never pretend to be new book prices. Public limit 20 connections/min; documentation lists both 25 and 30 channels, so the conservative allowance is 25. Firmware uses three channels on one socket. [Book schema and real-time F0](https://docs.bitfinex.com/reference/ws-public-books), [timestamp configuration](https://docs.bitfinex.com/docs/ws-general), [public limits](https://docs.bitfinex.com/docs/requirements-and-limitations). |
| Gemini | `wss://ws.gemini.com`; `{"id":"1","method":"subscribe","params":["btcusd@trade"]}` | Public USD trade-only stream; exact instrument, execution nanoseconds and increasing trade ID required. Integer timestamp parsing preserves millisecond precision. The verified connection's `conninfo` advertised request weight 7,000/10 s and 300 connection attempts/5 min; these are observed live allowances, not a permanent promise. One subscription and conservative retries remain far below them. JSON status 429 receives at least a minute of backoff. [Production endpoint](https://developer.gemini.com/trading/websocket/introduction), [trade schema](https://developer.gemini.com/trading/websocket/streams), [connection-info method](https://developer.gemini.com/trading/websocket/playground). |

All six feeds were reached by computer probes. The 2026-10-13 added-feed
probe received 279 Binance trades and one Gemini trade in 30 seconds and
saved the exact payloads and Gemini live quotas in
[the stream recovery report](../test/stream_recovery_probe_report.json).
Separate TLS handshakes for both hosts passed with the embedded Mozilla roots
using TLS 1.2 / ECDHE-RSA-AES128-GCM-SHA256. Sparse exchanges remain useful
fallbacks; reception volume is measured rather than inferred from a timer.

A 60-second probe tested Bitfinex's ticker before choosing its book. The
ticker produced only five updates with a maximum 15.184-second gap. Using
its last trade or ticker-derived midpoint with Coinbase gave 370/495 changed
frames and 296/495 multi-digit changes after warmup. That feed was rejected
for the preferred pair. See [the ticker probe](../test/bitfinex_ticker_probe_report.json).

The subsequent 60-second verified-TLS probe used the exact firmware HTTP
upgrade headers and embedded Mozilla roots. Coinbase supplied 190 executed
trade prices; the same Bitfinex socket supplied 71 valid depth-one BBO
midpoints, with a maximum 4.406-second gap. Replaying the actual compiled
`QuoteBook` and `PriceGuard` at 100 ms after ten seconds of warmup produced
495/495 whole-dollar changes and 495/495 multi-digit changes. Maximum selected
receipt age was 4.209 seconds. Both sockets remained connected with no errors
and two successful PING/PONG exchanges each. This computer measurement tests
the observed price alternatives and selector; ESP32 timing/reception still
requires separate device acceptance. See [the book probe](../test/bitfinex_book_probe_report.json)
and [its complete captured book sequence](../test/live_fixtures/bitfinex_book_session.json).

The BBO midpoint is the arithmetic mean of actual best bid and ask prices,
not an executed trade, and is tagged `BboMidpoint` throughout the display
selector and diagnostics. Its timestamp describes publication. The optional
inner ticker `FIRST_TRADE` field is never interpreted as freshness. A separate
low-volume comparison verified all eleven REST sources again and normalized
USDT quotes with Kraken's observed USD rate; Bitfinex showed a persistent
venue premium during that sample. See [the comparison](../test/venue_price_comparison_report.json).
Canonical chart-source selection is independent of Nixie quote diversity so
that venue spread does not become chart volatility. See
[refresh and chart semantics](NIXIE_REFRESH.md).

Bitfinex's `UST` identifies Tether USDt, verified against its
[official currency label map](https://api-pub.bitfinex.com/v2/conf/pub:map:currency:label).
`BTCUSD`, `BTCUST` and `USTUSD` were present in the
[public exchange pair list](https://api-pub.bitfinex.com/v2/conf/pub:list:pair:exchange)
on 2026-10-13. The implied USD quote multiplies the current BTC/USDT midpoint
by the current USDT/USD midpoint. Both complete bound books must have a spread
of at most 1%, and the FX levels must lie within 0.5–1.5 USD. Their publication
and receipt times remain separately stored. A genuine data update in either
leg may publish a `ConvertedBboMidpoint` with the latest actual leg publication
timestamp; heartbeat messages publish no quote. Its value is an implied current
quoted midpoint, never an executed trade or an invented price offset. Converted
quotes are display candidates only and never add observations to the canonical
USD candle chart.

The initial two-feed anti-repeat computer probe produced zero A→B→A reversals
but held 213/788 transitions when no permitted real alternative existed.
It is preserved in [its report](../test/anti_repeat_quote_probe_report.json).
The subsequent three-book probe uses the actual C++ parsers, guard and selector,
with the same two verified TLS sockets; its full measurement and source hashes
are in [the three-book report](../test/anti_repeat_three_books_probe_report.json).
Its 500 measured transitions all changed whole dollars, including 444
multi-digit changes, with no reversals or holds. All 597 nonblank frames,
including startup, traced to accepted real quotes and nearest-dollar rounding.
Run `node tools/probe_anti_repeat.mjs --bitfinex-cross` to repeat it. These
computer observations do not establish ESP32 timing or visible output.

An unchanged subscribed book remains current while the ordered connection is
healthy. Firmware publishes a `current_books_mask` only after the timestamp
configuration, exact subscription acknowledgement and complete snapshot are
validated, and both best prices form an uncrossed spread of at most 1%.
The quote selector may retain only that provider's latest direct and converted
book heads under their respective live-validity masks
beyond the usual five-second receipt lifetime. Older entries never gain this
exception. Missing sides, crossed or excessive spreads, malformed/stale data,
unsubscribe, disconnect, pause or more than 35 seconds without a message on that
book's own channel clear the mask. A later valid complete BBO can restore it.
Converted eligibility is published separately as `current_converted_books_mask`;
both component channels must remain valid and live. Activity on a different
channel or a socket PONG cannot hide an inactive conversion leg. A small ledger
preserves the latest direct and converted book heads independently of the
historical quote ring.

Book heartbeats maintain connection liveness without generating price ticks
or changing receipt time, publication time or quote counters. Thus telemetry
can honestly show an old `display_quote_age_ms` while the current bound book
continues to quote that same midpoint. Received book updates still must pass
the existing five-second publication-age check. A healthy unchanged book is
exempt from the 120-second no-new-quotes reconnect deadline; the 35-second
message-liveness deadline remains mandatory. This prevents an idle best bid
and ask from unnecessarily closing a valid stream for a REST variety request.

The earlier Coinbase/Kraken computer experiment remains in
[its report](../test/usd_pair_frames_report.json). It demonstrated 546/546
changed transitions, including 516 multi-digit changes, during a different
60-second market sample. It is retained as historical evidence rather than
a promise of exchange price differences at every future display deadline.

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
