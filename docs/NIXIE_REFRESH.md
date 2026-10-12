# Nixie refresh and chart behavior

Version 1.0.17 addresses the assembled 0.5 s / 5 min failure: sequential
HTTP jobs could hold `busy` continuously, preventing stream reconnects and
causing stale-price blanking. After each HTTP job, missing streams get a
35-second reconnect grace. Ordinary HTTP requests release the secondary but
never close the primary; a failed local RAM reservation defers without marking
a provider failure. Quotes use filtered 2 KiB JSON, and primary Bitstamp history
requests use at most 24 candles per page. TLS reserves two separate 18 KiB
record allocations and 24 KiB additional context/crypto headroom under the gate.

Range-sized backing arrays retain 7 / 32 / 62 / 290 / 170 samples instead of
300 samples for each of five caches, reclaiming about 11 KiB of static data
RAM. Bounded cache decoding/merging prevents overflow; worker values still own
a complete 300-sample response. Saved cache files remain compatible.

The normal price timer writes all six modules at the selected 500 ms, 2 s,
30 s, 5 min or 1 h deadline. Three distinct rounded prices per provider remain
eligible for five seconds. Selection prefers genuine multi-digit changes,
then a different provider and fresher receipts. Repeated updates refresh the
same entry instead of evicting alternatives. If no candidate changes the
whole-dollar display, the timer still runs and requests another provider in
the background. No synthetic $1 stepping or larger invented change is used.
Actual exchange prices determine how many digits differ.

The graph retains the 21 UTC time buckets and 13-row price scale. It connects
real anchors at normal candle spacing into a raster line; longer gaps remain
open. Connector pixels are drawing geometry, never persisted prices. A ring
of 64 real five-second samples makes the short chart denser as the clock runs.
Existing timestamped flash caches remain compatible and restore immediately.

Wireless diagnostics now report the active HTTP kind/source, last HTTP status,
requested differing quote, cached candle count/epoch, chart anchor mask and
rendered pixel count, alongside usable data-RAM and frame timing. A 30-second
`cadence-test` temporarily selects the normal 500 ms live-price path, marks
`cadence_test=true`, and restores the mechanical setting automatically.

The prior 1.0.15 bare-ESP32 measurement passed its 30-second diagnostic but did
not exercise ongoing minute-history requests on the assembled 5-minute range.
It is preserved in [that release report](../test/device_release_1.0.15_report.json).
The new starvation regression checks the missing coverage explicitly.

Run `python3 tools/run_tests.py` and `pio run`. Tests exercise actual firmware
sources with modeled network/hardware, address/undefined-behavior sanitizers,
all five refresh intervals, real-price selection and duplicate/stale handling,
HTTP reconnect grace and allocation deferral, bounded history pages, connected
line rendering and outage gaps, saved history/backfill, OTA and TLS checks.
LAN timing and pixels measure firmware commands; visible tubes/matrix still
require inspection because eXixe provides no MISO/cathode feedback.

The preserved protocol, CS pins and matrix mapping follow the working demo.
See [the manufacturer protocol](https://github.com/dekuNukem/exixe/blob/master/technical_details.md),
[PCB mapping](PCB_PIN_MAP.md), [endpoint documentation](SOURCES.md), and
[wireless collection/update commands](WIRELESS_DIAGNOSTICS.md).
