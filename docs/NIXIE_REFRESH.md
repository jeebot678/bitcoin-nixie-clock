# Nixie refresh and chart timing in 1.0.8

The frequency selector now controls a separate tube timer. At 0.5 s, 2 s,
30 s, 5 min or 1 h, the main loop samples the newest independently accepted
market price and sends all six tube modules their normal `0xAA` brightness
packets. No blank/blink animation is added. Whole-dollar rounding can leave
the visible digits unchanged even though a new frame was sent. Changing the
frequency schedules an immediate frame and starts the new interval; changing
the chart range does not change that interval.

Earlier firmware displayed quotes on arrival and suppressed unchanged digit
packets. Quotes, FX, history and OTA also shared one serialized HTTP worker.
Consequently neither network completion times nor tube traffic followed a
separate display cadence. An unconfirmed selector silently defaulted to the
30-second position. The corrected frequency selector defaults to 0.5 seconds
until a valid debounced detent is read, and `status` labels that condition
`frequencyDial=unconfirmed`. This fallback is not evidence that its wiring or
calibration is correct. Range remains JSEL1/GPIO34; frequency is JSEL2/GPIO35.

## Market reception

One certificate-verified TLS stream runs in its own FreeRTOS worker, using
Kraken BTC/USD ticker updates with the `bbo` trigger and Coinbase BTC-USD ticker
as failover. The displayed value is the exchange's last trade price, not the
bid/ask midpoint. A fixed, one-element queue overwrites older waiting ticks
with the latest value; heartbeat/acknowledgment traffic never becomes a price.
Payloads require the exact symbol, finite positive last price and a fresh
exchange timestamp. Startup and large outliers still require independent
provider confirmation.

The eleven REST providers remain available for confirmation and fallback. A
fresh stream suppresses redundant ticker polls; a quiet/disconnected stream
allows the ordinary dial-scheduled REST requests. Provider cooldowns and error
backoff remain enforced. Historical candles remain on the HTTP worker and do
not prevent incoming stream ticks or scheduled tube writes. If no fresh market
data exists, refreshes use the last accepted value until the existing outage
timeout blanks the tubes. No fabricated price movement is generated.

A computer-side 60-second check on 2026-10-11 confirmed both proposed USD
streams and captured their actual payloads for host regression tests. Kraken
delivered 77 ticker messages; Coinbase delivered 61. Both had only two distinct
prices, and their longest message gaps were 5.77 s and 4.62 s. This illustrates
why a 500 ms **display** timer cannot imply a different market price or a new
trade every 500 ms. This probe verifies schema/availability, not ESP32 timing.
Run `node tools/probe_device_feeds.mjs` to repeat it; the report goes to
`test/.build/device-feeds/report.json`.

Physical testing of 1.0.7 exposed a watchdog restart during concurrent TLS
work, so its OTA rollout was withdrawn to 1.0.6. Version 1.0.8 coordinates
connection opening with a shared lease held through the WebSocket upgrade.
Established stream reception continues during HTTP work. The HTTP worker
shares the idle task's priority, allowing expensive TLS calculations to run
without starving the idle watchdog. Its handshake deadline is 20 seconds;
the live stream retains its separate worker and a six-second TLS deadline.
TLS reads explicitly yield while waiting for missing bytes, including Arduino
2.x's otherwise busy-polling `Stream::timedRead`. Both workers have 16 KiB
stacks; `status` reports the stream's measured minimum remaining stack space
in bytes. No watchdog or certificate checks are disabled.

A UART transmit buffer queues full status reports so verbose USB diagnostics
do not block the display loop while bytes drain at 115200 baud.

The pinned WebSockets library uses its bounded handshake timeout through a
small constructor adapter, scoped to its client translation unit by a
PlatformIO build middleware. HTTP read/connect timeouts remain bounded. The
certificate index is retained and protected across simultaneous connections;
closing a REST connection cannot free the live stream's trust store. Certificate
signatures, dates and hostnames remain validated. Before OTA, the stream closes
to release TLS memory; reception resumes after the check/download. Tube timing
continues while OTA is in progress, subject to ordinary loop/flash latency.

## Matrix time mapping

The 21 columns divide `[now - selected range, now]` into equal UTC time
buckets. Column 0 is the oldest; column 20 includes the current instant.
The latest actual timestamp within each bucket selects its price. An older
live trade cannot replace a newer closed candle in the same bucket. Expired
and future points are excluded; missing data remains blank. Historical candle
resolution can leave some short-range buckets empty until real data exists.

| Range | Time per column |
| --- | --- |
| 5 minutes | 14.286 seconds |
| 30 minutes | 1 minute 25.714 seconds |
| 1 hour | 2 minutes 51.429 seconds |
| 24 hours | 1 hour 8 minutes 34.286 seconds |
| 7 days | 8 hours |

The vertical axis scales real prices into 13 rows, with higher prices nearer
the top and a small minimum span to avoid amplifying tiny movements. Fresh
quotes and range changes request a new plot immediately. Time also advances
the plot at least once per second even when the price is unchanged. The
driver sends matrix traffic only when the resulting pixels change. The
weekly graph can therefore hold its pixels while the Nixies keep refreshing.

## Verification

Sanitized host tests exercise the actual application and physical SPI packet
encoder at every frequency. They assert the newest accepted value is sampled
at the deadline, no early update leaks through, unchanged digits still send six
packets, a blocked history/TLS request does not stop the timer, dial changes
apply immediately, stale prices blank, and delayed loops/`millis()` rollover
preserve cadence without invented catch-up frames. Stream tests use captured
payloads and cover TLS configuration, bounded queues, malformed/stale/wrong
symbols, acknowledgments, heartbeat loss, provider failover, HTTP 403/429
cooldowns, Wi-Fi/SNTP gating, task-allocation failure and OTA pause. A concurrent
TLS regression verifies another connection's detach/reinitialization cannot
invalidate an active verifier.

Chart tests cover the first and last second in every one of the 21 buckets
at all five ranges, expiry/future exclusion, all 13 rows' bounds and price
ordering, and newer-candle/older-trade selection.

Use serial `ticks` to toggle frame logging, or `ticks on` / `ticks off` for
idempotent control. Each line includes its monotonic
time, selected interval, actual frame gap and quote age. `status` includes the
frame count, displayed sampled value, selector confirmation, stream state and
quote counters. The final image's three-minute USB check observed 358 frames
with gaps of 497–503 ms (median 500 ms), 48 distinct sampled prices, advancing
Kraken quotes, three completed history requests, nine cache saves and a
successful OTA manifest check without a watchdog restart. A failed history
request recovered through Bitstamp; reception resumed after the OTA check.
The measurement and the exact flashed image's SHA-256 are recorded in
[`test/device_refresh_report.json`](../test/device_refresh_report.json).

Repeat the hardware measurement with the assembled device's frequency dial
at 0.5 s; the tool reads diagnostics without changing either selector:

```sh
~/.platformio/penv/bin/python tools/verify_device_refresh.py \
  --port YOUR_ESP32_PORT --version 1.0.8 --duration 180
```

The verifier requires advancing connected-stream and accepted-price counters,
the requested interval, at least 20 frames and no recorded device fault. It
rejects a frame delayed over 10 ms beyond its selected period and writes an
aggregate JSON report plus an ignored raw serial log. It does not establish a
new trade every 500 ms, visible tube behavior, every physical dial detent,
full-size 24-hour TLS memory use or power-loss rollback. Those remain assembled
device acceptance checks.

Official protocol and connection limits are linked in [SOURCES.md](SOURCES.md).
