# Independent live-price benchmark

This records the 1.0.6 computer experiments. Firmware 1.0.7 now adds one
independent USD stream with failover and a separate Nixie display timer;
see [the refresh correction and device checks](NIXIE_REFRESH.md). The measured
eight-stream computer throughput below is not an ESP32 performance claim.

The computer-side simulation met the requested 500 ms quote-delivery target.
It uses real public market data, runs without an ESP32, and requires no API keys,
subscriptions or npm packages. The recorded runs took place on 2026-10-10 in
America/New_York (2026-10-11 UTC), using Node 25.9.0.

| Experiment | Run / warmup | Accepted quotes/s after warmup | Longest gap between quotes | Samples receiving a new quote | Maximum sample interval | 500 ms target |
| --- | --- | --- | --- | --- | --- | --- |
| Serial REST, new connection, firmware cooldowns | 40 s / 10 s | 1.83 | 1,189.99 ms | 50/75 | 401.88 ms | Failed |
| Concurrent REST, persistent connections, faster cooldowns | 60 s / 10 s | 9.74 | 378.39 ms | 125/125 | 401.33 ms | Passed |
| Concurrent REST plus eight public trade streams | 180 s / 10 s | 28.11 | 274.14 ms | 425/425 | 402.49 ms | Passed |

All eleven firmware REST providers returned valid prices in every experiment:
Coinbase, Kraken, Bitstamp, Gemini, Bitfinex, Binance, OKX, KuCoin, Gate, Bitget
and MEXC. All eight configured streams delivered real trade updates. Bybit was
probed as a twelfth REST candidate, returned HTTP 403, and was excluded from
capacity with a one-hour cooldown. There were no HTTP 429/418 responses or
recognized JSON rate-limit errors in these runs. The final stream run had no
WebSocket errors.

The stream experiment's 425 post-warmup samples each had at least one newly
accepted quote and a receipt age below 500 ms; the greatest receipt age was
237.10 ms. A newly received stream trade occurred in 366 samples, and the
rounded whole-dollar display changed in 391 samples. A valid REST response can
report an unchanged last trade: receipt frequency, trade frequency and visible
digit changes are separate measurements. Different exchanges also have
different last prices; a display change is not proof of a global market move.

## Reproduce

Use Node with built-in `WebSocket` support; the measured runs used Node 25.9.0.
Run the modes sequentially, rather than doubling a provider's load with
simultaneous benchmarks:

```sh
node tools/simulate_prices.mjs --mode baseline --policy firmware --seconds 40 --output test/.build/price-benchmark-baseline
sleep 5
node tools/simulate_prices.mjs --mode rest --policy fast --seconds 60 --output test/.build/price-benchmark-rest
sleep 5
node tools/simulate_prices.mjs --mode stream --policy fast --seconds 180 --output test/.build/price-benchmark-stream
```

Allow at least five seconds between runs because provider cooldown state is
per process. `--interval-ms 500` is the default target. Display sampling happens
every 400 ms to allow timer jitter below that target. Each run writes
`MODE.events.jsonl`, `MODE.samples.jsonl` and `MODE.report.json` under its output
directory. The test succeeds only when **every** post-warmup sample has a new
accepted quote, receipt age at most the target, and sample spacing at most the
target; average throughput alone cannot pass it. Delayed sampling does not
create fabricated catch-up samples.

[`test/price_benchmark_report.json`](../test/price_benchmark_report.json) retains
the measured results and provider statistics. Aggregate quote and sample
metrics exclude warmup and late replies received while draining requests at
shutdown. Provider success counts include those completed requests. Raw logs
remain in the ignored `test/.build/` directory and can be regenerated.

## REST scheduling and limits

The simulator reads the eleven endpoints directly from
`include/PriceSources.h`; it adds only the diagnostic Bybit candidate. Endpoint
schemas and official REST quotas are documented in [SOURCES.md](SOURCES.md).
The `firmware` policy retains the actual five-second provider cooldowns. The
`fast` policy uses at most one request per second per provider, leaving MEXC
and Bybit at five seconds. This is below the documented public quotas,
including Kraken's recommendation of no more than one request per second,
Gemini's 120 requests/minute and Bitfinex's 90 requests/minute.

Concurrent modes allow at most four REST requests in flight and one connection
per REST provider, starting at most one request each 100 ms with the fast
policy. Baseline permits only one request in flight, starts no faster than
the selected target and disables connection reuse. It approximates the
firmware's serialized connection strategy, but Node uses HTTP/1.1 while the
firmware uses HTTP/1.0. It does not model ESP32 CPU, TLS memory or Wi-Fi latency.

HTTP 429/418 and recognized JSON rate-limit errors impose at least a minute of
backoff; `Retry-After` can extend it. HTTP 401/403/404/451 impose an hour;
other failures use exponential backoff. Rotation distributes requests within
each provider's limits. A blocked provider is not used or bypassed.

## Public stream endpoints and control-message limits

These subscriptions were checked against official documentation and tested
live. Each uses one connection and one subscription request. Market updates
are pushed by the exchange, rather than obtained by sending a request for
every tick. Client message quotas are distinct from incoming market traffic.

| Provider | Endpoint and subscription | Documented rule / implementation |
| --- | --- | --- |
| Coinbase | `wss://ws-feed.exchange.coinbase.com`; `ticker` and `heartbeat`, `BTC-USD` | `ticker` updates on matches; `ticker_batch` has a five-second cadence and is not used. Client messages: 10/s, burst 1,000. [Channels](https://docs.cdp.coinbase.com/exchange/websocket-feed/channels), [limits](https://docs.cdp.coinbase.com/exchange/websocket-feed/rate-limits). |
| Kraken | `wss://ws.kraken.com/v2`; `trade`, `BTC/USD` and `USDT/USD`, `snapshot:false` | About 150 connection attempts per rolling 10 minutes/IP; reconnect no faster than five seconds after downtime. Simulator backs off 5–60 seconds. [Trade schema](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/trade), [connection rules](https://docs.kraken.com/exchange/guides/websockets/introduction). |
| Bitstamp | `wss://ws.bitstamp.net`; `live_trades_btcusd` | Official public trade subscription tested live. No numeric quota was established from the retrieved page; use one connection and conservative reconnection, rather than assuming unlimited access. [Official WebSocket documentation](https://www.bitstamp.net/websocket/v2/). |
| Bitfinex | `wss://api-pub.bitfinex.com/ws/2`; `trades`, `tBTCUSD` | 30 public channel subscriptions/connection. Parse `te` executions, ignore duplicate `tu` updates and heartbeats. [Trade schema](https://docs.bitfinex.com/reference/ws-public-trades), [general rules](https://docs.bitfinex.com/docs/ws-general). |
| Binance | `wss://data-stream.binance.vision/ws/btcusdt@aggTrade` | 5 incoming client control/ping/pong messages/s, 1,024 streams/connection, 300 connection attempts/5 minutes/IP; connection rotation at 24 hours. No subscription control traffic for this URL. [Official stream specification](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md). |
| OKX | `wss://ws.okx.com:8443/ws/v5/public`; `trades`, `BTC-USDT` | 3 connection requests/s; 480 subscribe/unsubscribe/login requests per connection/hour. Simulator sends `ping` every 20 seconds, ignores subscription acknowledgments. [Official WebSocket rules and trades](https://app.okx.com/docs-v5/en/). |
| Gate | `wss://api.gateio.ws/ws/v4/`; `spot.trades`, `BTC_USDT` | Public channel needs no authentication. One subscription; `spot.ping` every 20 seconds. No numeric public trade-subscription quota was established from this page; trading-order quotas do not establish a market-stream allowance. [Official specification](https://www.gate.com/docs/developers/apiv4/ws/en/). |
| Bitget | `wss://ws.bitget.com/v2/ws/public`; `SPOT`, `trade`, `BTCUSDT` | 10 client messages/s; 300 connection requests/5 minutes/IP; 100 connections/IP; 240 subscription requests/hour/connection. Simulator sends `ping` every 20 seconds and ignores snapshots. [Rules](https://www.bitget.com/docs/classic/websocket/intro), [trade schema](https://www.bitget.com/zh-CN/api-doc/classic/spot/websocket/public/Trades-Channel). |

## Accuracy and practical limits

REST uses certificate validation with the firmware's Mozilla roots; WebSockets
use Node's trusted roots. REST bodies have bounded deadlines and size, must
be JSON, and must have a recent HTTP `Date` and no excessive cache `Age`.
Provider timestamps and stream trade timestamps are checked for staleness
and future values. Heartbeats, subscription acknowledgments and supported
initial snapshots do not count as new trade updates. Repeated consecutive
stream trade IDs are discarded; Bitfinex's execution/update pair counts once.
USDT prices require a recent real Kraken USDT/USD rate. Startup and large
outliers require agreement from two independent providers.

Some REST endpoints provide no trade timestamp, so a fresh HTTP response
cannot establish their internal publication delay. Gemini returned only four
distinct prices during the three-minute stream experiment; that observation
does not prove it can publish every 500 ms. Stream timestamps and trade IDs
give stronger evidence of actual new market activity. The simulator counts
valid responses separately from streamed trades and leaves exchange timestamps
null where unavailable. It does not invent a new trade when price is unchanged.

These bounded runs demonstrate availability and timing on this network; they
do not guarantee every future 500 ms deadline during an outage or quiet market.
Firmware 1.0.6 contains the requested rotary swap and one-hour frequency
detent, but retains its single HTTPS worker. Code inspection shows that quote,
FX, history and OTA requests share that worker, with a new TLS connection for
each request. The baseline missed 500 ms without rate-limit responses. This
supports connection setup and serialized work as bottlenecks; it does not
identify the cause of an unobserved device failure. The eight simultaneous
TLS streams in this computer experiment have not been ported to or validated
on the ESP32.
