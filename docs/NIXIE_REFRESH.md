# Nixie refresh correction in 1.0.7

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

The pinned WebSockets library uses a six-second handshake timeout through a
small constructor adapter, scoped to its client translation unit by a
PlatformIO build middleware. HTTP read/connect timeouts remain bounded. The
certificate index is retained and protected across simultaneous connections;
closing a REST connection cannot free the live stream's trust store. Certificate
signatures, dates and hostnames remain validated. Before OTA, the stream closes
to release TLS memory; reception resumes after the check/download. Tube timing
continues while OTA is in progress, subject to ordinary loop/flash latency.

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

Use serial `ticks` to toggle frame logging. Each line includes its monotonic
time, selected interval, actual frame gap and quote age. `status` includes the
frame count, displayed sampled value, selector confirmation, stream state and
quote counters. Device checks must measure actual five-detent voltages, visible
digits, frame intervals and simultaneous stream/HTTP/history heap use; the
ESP32 is disconnected during this software fix.

Official protocol and connection limits are linked in [SOURCES.md](SOURCES.md).
