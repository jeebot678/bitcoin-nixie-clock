# Nixie refresh and chart behavior

Firmware 1.0.22 keeps the selected display cadence independent of network
requests and preserves a continuous supply of genuine price alternatives.
Frequency settings are 100 / 500 / 1500 / 10000 / 60000 ms; range settings are
30 / 60 / 300 / 1800 / 3600 seconds. Each deadline writes all six Nixie modules,
selecting a recent validated quote even when a request is still running.

The quote book retains up to twelve distinct rounded prices per provider for five
seconds. Selection prefers a genuine multi-digit change, then a different
provider and fresher receipts. Repeated receipts refresh the current entry
rather than evicting its alternatives. Quotes can be reused within that
lifetime, so a display change does not mean a newly executed trade. If no
candidate changes the whole-dollar display, the timer still runs. Version
1.0.22 lets healthy streams supply alternatives before requesting another
eligible HTTP provider: an unchanged run must last at least five seconds,
or the selected display interval if longer. A genuine change resets that
wait; stale feeds and required independent confirmation bypass it. An
isolated unchanged 100 ms frame therefore cannot interrupt a second feed
for a slower HTTP request. No synthetic $1 stepping or larger
invented change is used; all displayed values come from observed market prices.

Coinbase is now the preferred primary USD stream, with Kraken preferred on
the secondary. Kraken adds matched-trade batches and USDT/USD conversion on
its existing TLS connection. Binance and Gemini join Bitstamp and Bitfinex
as stream fallbacks. Both workers retain 16 actual ticks each; a burst no
longer overwrites every intermediate price before the main loop can inspect
it. The main loop drains up to 32 ticks with a four-millisecond work budget
so burst processing does not delay the display timer. FX-only ticks update
conversion without counting as Bitcoin prices or masking stale Bitcoin data.
See [verified endpoints, public quotas and payloads](SOURCES.md).

HTTP dispatch holds new handshakes but preserves established subscriptions.
Only a real data-RAM reservation can release the secondary. Routine quote/FX
requests defer rather than close the primary. OTA and infrequent candle
bootstrap jobs may pause it when necessary. After HTTP work, missing feeds
receive a bounded 35-second reconnect grace, including a missing secondary
while the primary remains healthy. TLS still reserves two separate 18 KiB
record allocations plus 24 KiB context/crypto headroom under the shared gate.
This fixes the pattern where requests for a different price repeatedly tore
down the second stream and left a sparse fallback as the only live source.

Version 1.0.21 arms the heartbeat timer only after the WebSocket upgrade
completes. A delayed PONG does not disconnect a feed that is delivering
valid messages; the independent 35-second message and 120-second Bitcoin
quote deadlines still detect dead or silent connections. Negative TLS
connection failures release the handshake gate immediately and retain their
error code for LAN diagnostics. A fallback can retry a preferred USD feed
after 30 seconds when another feed remains established, with exclusive
provider and recovery ownership preserving the other connection.

The matrix has 21 fixed UTC candles and 13 rows. Body and wick pixels are
fully on or off, without pulse dimming. Each displayed column has at least
one lit cell once a price is available. An observed candle includes its
open-to-close body and high-to-low wick; an unobserved column displays a
flat close hold without adding synthetic trades to its stored OHLC data.
Every validated live quote contributes highs/lows across all five ranges,
independently of the selected view and Nixie frequency. Bootstrap responses
also merge into every attempted range rather than replacing the selected
cache. Switching ranges preserves each cache and its scale.

Closed candles retain their UTC boundaries and scroll one column at a time.
Each range keeps its own price scale, with one-row panning at most every five
seconds, prompt expansion for significant spikes and a 30-minute smoothing
period for routine volatility changes. Candles and scale parameters
checkpoint at most every 30 seconds through immutable core-0 background
writes into separate CRC-protected files. Legacy history remains intact.
See [candle behavior, persistence and testing](CANDLE_CHART.md).

Wireless diagnostics report active feed providers, cumulative source quote
counts and receipt ages, queue overflows, rejected payloads and disconnects.
They also report HTTP kind/source/status, a requested differing quote, cached
candle count/epoch, chart columns/pixels, usable data-RAM and frame timing.
Per-deadline transition, changed-price and multi-digit-change counters observe
every 100 ms frame even when LAN collection is slower. The 30-second
`cadence-test` temporarily selects the normal 500 ms live-price path, sets
`cadence_test=true`, and restores the mechanical setting automatically.

The separate 2026-10-13 computer test used Coinbase USD ticker plus Kraken
USD BBO ticker and a JavaScript replica of the firmware selector. During a
60-second run, after five seconds of warmup, it received 325 Coinbase and
832 Kraken prices and emitted 547 frames. All 546 transitions changed the
whole-dollar value, including 516 multi-digit changes, with no empty frames
and 99–102 ms timer gaps. The test used observed quotes and the same
12-entry/five-second selection policy. This is evidence that the preferred
pair supplied sufficient alternatives in that sample, not an ESP32 timing
measurement or a promise of new market executions at every deadline.
See [the computer report](../test/usd_pair_frames_report.json). Device
acceptance is recorded separately for each installed release.

The first 1.0.20 assembled OTA run exposed further feed disconnects: its
100 ms timer was accurate, but only 46.3% of 1,797 transitions changed the
price, with 6.12 incoming prices/s and a 28-second quote gap. All 21 columns
remained displayed. The run includes startup and is retained as failed in
[the device report](../test/device_release_1.0.20_report.json). A follow-up
fix and another device measurement are required; the computer proof alone
is insufficient to accept this behavior.

The 1.0.21 sustained device run improved reception to 7.63 prices/s and
changed 1,669 of 1,800 transitions (92.7%). It still reclaimed the second
stream for HTTP after isolated unchanged frames, so the run is retained as
failed in [its report](../test/device_release_1.0.21_report.json). Version
1.0.22 adds the sustained-sameness holdoff described above.

The 1.0.18 assembled LAN measurement averaged 100.007 ms but found
33–167 ms individual gaps during synchronous checkpoints. The result is
preserved in [the report](../test/device_release_1.0.18_report.json) as a failed
timing check. Version 1.0.19 moves immutable checkpoint writes to the existing
background worker without pausing streams or changing source cooldowns.

The 1.0.19 assembled repeat passed: 1,798 frames over 179.737 seconds
averaged 99.965 ms, with individual 89–111 ms gaps, no connection errors,
19–21 populated candle columns and advancing cache checkpoints. Of those
transitions, 1,423 changed the rounded price and 1,112 changed multiple digits.
The report retains these counts rather than treating every hardware refresh
as a new market price. See [the device report](../test/device_release_1.0.19_report.json).
Visible-output verification remains false because there is no feedback from
the display hardware.

The prior 1.0.15 bare-ESP32 measurement passed its 30-second diagnostic but did
not exercise ongoing minute-history requests on the assembled 5-minute range.
It is preserved in [that release report](../test/device_release_1.0.15_report.json).
The new starvation regression checks the missing coverage explicitly.

Run `python3 tools/run_tests.py` and `pio run`. Tests exercise actual firmware
sources with modeled network/hardware, address/undefined-behavior sanitizers,
all five refresh intervals, real-price selection and duplicate/stale handling,
stream burst preservation, FX-only freshness, fallback schemas/backoff, HTTP
reconnect grace and memory deferral, bounded transaction parsing, fixed OHLC
candles, stable scales, full-brightness pixels and close holds, saved
candles/backfill, range switching, OTA and TLS checks.
LAN timing and pixels measure firmware commands; visible tubes/matrix still
require inspection because eXixe provides no MISO/cathode feedback.

The preserved protocol, CS pins and matrix mapping follow the working demo.
See [the manufacturer protocol](https://github.com/dekuNukem/exixe/blob/master/technical_details.md),
[PCB mapping](PCB_PIN_MAP.md), [endpoint documentation](SOURCES.md), and
[wireless collection/update commands](WIRELESS_DIAGNOSTICS.md).
