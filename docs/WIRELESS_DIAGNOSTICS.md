# Wireless diagnostics and OTA control

Firmware 1.0.11 exposes a LAN service at `http://bitcoin-clock.local:8080`.
If multicast discovery fails, use the clock's IP on port 8080. It starts when
home Wi-Fi is connected and the setup portal has closed. The passwordless
provisioning hotspot still closes normally; diagnostics remain on home Wi-Fi.
No USB or external telemetry service is required.

The server runs in its own 6 KiB task on core 0 at idle priority. It reads
bounded, copied snapshots and queues commands, without touching display buses
or calling the updater on the display's core. The main loop publishes every
250 ms; the computer collects every 500 ms.

`GET /telemetry` reports firmware/device identity, snapshot age, both calibrated
millivolt readings and raw ADC counts, decoded and selected contacts, defaults,
expected voltage centers, frequency/range, market/stream counters, quote age,
commanded digits, price-frame counters/gaps, heap, HTTP/OTA state and boot-health
confirmation. It distinguishes price, startup, blanking and tube-test modes.
`decoded=-1` means invalid input; an old `selected` can be retained.
`initialized=false` means the setting remains a default. No Wi-Fi credentials
or control token are included in samples.

Hold BOOT for one second and release to toggle matrix diagnostics. Short taps
cycle selected settings, voltages, quote/price-frame counters, IP octets and
a five-second tube test. `F0309` above `R0926` means frequency 309 mV and range
926 mV. A `?` means the current input is unconfirmed or differs from the held
setting. The IP's first pair of octets appears for two seconds, followed by
its last pair. Five-second BOOT hold still reopens Wi-Fi setup; provisioning
status takes priority during setup.

## Computer collector

The Python standard-library collector runs on the same LAN as the clock and
retries discovery/reconnections, including OTA reboots. Pass a fallback address
with `--fallback-host YOUR_ESP32_IP` before the operation. It remains running
while this computer is awake; it does not install a login/startup service.

```sh
python3 tools/clock_link.py start
python3 tools/clock_link.py status
python3 tools/clock_link.py mark frequency-position-1
python3 tools/clock_link.py readings
python3 tools/clock_link.py tube-test
python3 tools/clock_link.py chart
```

Files under the ignored `test/.build/telemetry/` directory are accessible to
Codex in this workspace on later turns:

- `latest.json`: latest successful snapshot and receipt time.
- `connection.json`: current availability; check before treating an older
  `latest.json` as live data.
- `samples.jsonl`: timestamped samples and optional position labels.
- `samples.previous.jsonl`: previous samples after 10 MiB rotation.
- `collector.log` / `collector.pid.json`: background process status.

The collector records independently; it does not make Codex keep working
after a conversation ends. For calibration, move one dial through all five
contacts, holding each at least three seconds, while leaving the other dial
still. Label positions with `mark`, or tell Codex which dial/position is set.
Use observed values to update the separate calibration arrays, then publish
another signed firmware version.

## Triggering an update

```sh
python3 tools/clock_link.py ota --expect 1.0.12 --wait 240
```

First publish and verify the signed newer release and `main` manifest. The tool
obtains an ephemeral token and queues an immediate OTA check, bypassing the
six-hour schedule. Existing HTTP work finishes first, and the current boot
must be health-confirmed. Signature, board, version, hash, trusted download
hosts, retry policy and rollback checks remain enforced. No arbitrary firmware
URL or unsigned image is accepted. `--expect`/`--wait` confirms that the requested
version is running and health-confirmed.

The browser provides the same voltage, chart, tube-test and update controls.
POSTs require the token and reject unrelated browser origins. GETs need no
token. A first installation from 1.0.10 or earlier uses the existing boot-time
OTA check: power-cycle with BOOT released after publishing the new manifest.
Once 1.0.11 is installed, updates can be triggered remotely without opening
the clock.

Host tests exercise actual handlers, queue bounds/ownership, authorization,
network/portal gating, mDNS failure, rollover, main-loop commands, BOOT gestures
and numeric matrix glyphs. The collector is tested against a real local HTTP
server for fallback/reconnect, malformed/stale/oversized data, collection,
labels, rotation and concurrent writes. Counters measure commanded output,
not cathode light: eXixe has no MISO acknowledgment. Use the visible tube test
to check assembled hardware.
