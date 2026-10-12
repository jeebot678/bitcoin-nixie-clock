# Nixie refresh and chart behavior

Version 1.0.17 addresses the assembled 0.5 s / 5 min failure: sequential
HTTP jobs could hold `busy` continuously, preventing stream reconnects and
causing stale-price blanking. After each HTTP job, missing streams get a
35-second reconnect grace. Ordinary HTTP requests release the secondary but
never close the primary; a failed local RAM reservation defers without marking
a provider failure. Quotes use filtered 2 KiB JSON, and primary Bitstamp history
requests use at most 24 candles per page. TLS reserves two separate 18 KiB
record allocations and 24 KiB additional context/crypto headroom under the gate.

Version 1.0.18 maps frequency to 100 / 500 / 1500 / 10000 / 60000 ms and
range to 30 / 60 / 300 / 1800 / 3600 seconds. Twenty-one OHLC records per
range replace the running point caches. Candles and per-range scale parameters
checkpoint every 30 seconds in separate CRC-protected files; legacy history
files remain intact. Candle bootstrap uses individual transactions in a
512-byte streaming JSON pool and may briefly pause the primary for its TLS
reservation when RAM requires it. Ordinary quote/FX jobs still preserve the
primary; the reconnect grace remains in place.

The normal price timer writes all six modules at the selected 100 ms, 500 ms,
1.5 s, 10 s or 1 min deadline. Twelve distinct rounded prices per provider remain
eligible for five seconds. Selection prefers genuine multi-digit changes,
then a different provider and fresher receipts. Repeated updates refresh the
same entry instead of evicting alternatives. If no candidate changes the
whole-dollar display, the timer still runs and requests another provider in
the background. No synthetic $1 stepping or larger invented change is used.
Actual exchange prices determine how many digits differ.

The matrix has 21 fixed UTC candles and 13 rows. Bodies are solid; wick-only
pixels are dimmed. Every validated live quote contributes real highs/lows
across all five ranges. Closed candles retain their boundaries and scroll by
one column. Each range keeps its own price scale, with one-row panning at
most every five seconds, prompt expansion for significant spikes and a
30-minute smoothing period for routine volatility changes. See
[candle behavior, persistence and testing](CANDLE_CHART.md).

Wireless diagnostics now report the active HTTP kind/source, last HTTP status,
requested differing quote, cached candle count/epoch, chart anchor mask and
rendered pixel count, alongside usable data-RAM and frame timing. Per-deadline transition, changed-price
and multi-digit-change counters observe every 100 ms frame even when LAN
sampling is slower. A 30-second
`cadence-test` temporarily selects the normal 500 ms live-price path, marks
`cadence_test=true`, and restores the mechanical setting automatically.

The prior 1.0.15 bare-ESP32 measurement passed its 30-second diagnostic but did
not exercise ongoing minute-history requests on the assembled 5-minute range.
It is preserved in [that release report](../test/device_release_1.0.15_report.json).
The new starvation regression checks the missing coverage explicitly.

Run `python3 tools/run_tests.py` and `pio run`. Tests exercise actual firmware
sources with modeled network/hardware, address/undefined-behavior sanitizers,
all five refresh intervals, real-price selection and duplicate/stale handling,
HTTP reconnect grace and allocation deferral, bounded transaction parsing, fixed OHLC candles, stable scales, outage gaps, saved candles/backfill, OTA and TLS checks.
LAN timing and pixels measure firmware commands; visible tubes/matrix still
require inspection because eXixe provides no MISO/cathode feedback.

The preserved protocol, CS pins and matrix mapping follow the working demo.
See [the manufacturer protocol](https://github.com/dekuNukem/exixe/blob/master/technical_details.md),
[PCB mapping](PCB_PIN_MAP.md), [endpoint documentation](SOURCES.md), and
[wireless collection/update commands](WIRELESS_DIAGNOSTICS.md).
