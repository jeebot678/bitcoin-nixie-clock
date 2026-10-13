# Bitcoin Nixie Clock

Live, standalone firmware for the Rev20 motherboard: ESP32-WROOM-32E, six exixe Nixie modules, a 13 × 21 LED matrix driven by six MAX7219 chips, and two five-position resistor-ladder selectors. The original working simulation is archived in `demo/main_demo.cpp`; the default firmware uses actual market data.

Version **1.0.19** maps frequency to **0.1 s / 0.5 s / 1.5 s / 10 s / 1 min** and range to **30 s / 1 min / 5 min / 30 min / 1 h** in physical lowest-to-highest order. Every column is an OHLC candle with a solid body and dimmer wick. Each range retains its own slowly moving price scale, with immediate widening for large spikes and a 30-minute smoothing period for routine volatility changes. Twelve distinct recent prices per provider are retained for selecting real multi-digit changes. Primary-feed protection, bounded parsing, saved caches and signed updates from `main` remain in place. See [wireless diagnostics](docs/WIRELESS_DIAGNOSTICS.md).

The clock receives live BTC/USD through two persistent public streams, preferring Kraken and Coinbase with Bitstamp/Bitfinex failover, and retains eleven independent free REST endpoints for confirmation and fallback. REST rotation honors provider cooldowns and `Retry-After`. USDT quotes are converted with a recent USDT/USD rate. No API keys, subscriptions, intermediary server or running web app are needed.

## Controls

| Position | Frequency: JSEL2 (GPIO35) | Range: JSEL1 (GPIO34) |
| --- | --- | --- |
| 1 | 0.1 seconds | 30 seconds |
| 2 | 0.5 seconds | 1 minute |
| 3 | 1.5 seconds | 5 minutes |
| 4 | 10 seconds | 30 minutes |
| 5 | 1 minute | 1 hour |

The Nixies sample the latest accepted price at the selected frequency, rounded to whole USD. Every scheduled refresh writes all six tube modules, including unchanged digits. Market reception runs independently of that display timer and historical downloads. Selecting a new frequency immediately reschedules the tubes. Until the frequency selector has a confirmed reading, the timer defaults to 0.1 seconds; `status` explicitly reports an unconfirmed dial. See [Nixie refresh behavior and validation](docs/NIXIE_REFRESH.md). The matrix divides each range into 21 fixed UTC candle buckets, each with an open, high, low and close. The last column is forming; closed candles scroll left at the next boundary instead of being rebucketed on every tick. The durations per column are 1.429 s, 2.857 s, 14.286 s, 85.714 s and 171.429 s. Bodies stay solid and wick-only pixels are dimmed. Empty buckets remain blank. Each range has a retained scale: ordinary updates preserve candle heights, the view pans at most one row per five seconds, significant spikes widen promptly, and routine width changes use a 30-minute moving average with small, infrequent adjustments. See [candle behavior and validation](docs/CANDLE_CHART.md).

Real individual trades bootstrap candles through a bounded 512-byte streaming JSON pool. Bitstamp minute/hour transactions are primary; Coinbase's newest 1,000 trades provide partial-history fallback. All five ranges aggregate validated live quotes regardless of the selected dial. A healthy stream avoids repeated downloads after bootstrap; reception gaps trigger repair. Overlapping transaction responses are merged without inventing data for missing buckets.

Snapshots use the existing 128 KiB SPIFFS partition. Each range has two alternating checksummed candle files, retaining OHLC values, exact UTC time and the scale. Checkpoints run at most every 30 seconds on the existing core-0 worker, using immutable copies while the display continues. Existing close-only cache files remain intact; fresh transactions establish true OHLC history. Application updates preserve Wi-Fi and cache files.

Without a battery-backed RTC, the ESP32 cannot know how long it was powered off until SNTP answers. Before synchronization, an available saved chart is shown at its recorded snapshot time; it is then immediately positioned against current UTC. Saved history never initializes the live price or the HTTPS clock. During Wi-Fi setup, the setup status still takes priority on the matrix.

Once a saved or freshly fetched chart is ready, the matrix performs a startup reveal once per boot: all 273 LEDs fill left to right, row by row from top to bottom, then clear in the same order while leaving the selected chart pixels lit. Each pass takes about 0.82 seconds, with a fully lit frame between them. The reveal follows current prices and dial selections and runs without delaying the main loop. Later range changes show their chart immediately without repeating it.

`include/DeviceConfig.h` contains independent calibration centers for each dial, debounce, intervals and brightness. Firmware 1.0.12 and later map the assembled switches from physical lowest to highest: P5 → P4 → P3 → P2 → P1 (12 kΩ → 6.8 kΩ → 3.9 kΩ → 1.8 kΩ → 0 Ω). A fresh Wi-Fi sweep measured frequency centers of 1774/1328/915/490 mV and range centers of 1775/1328/909/493 mV; the grounded contact reports raw ADC 0 and calibrated 142 mV on both. Its configured zero marker extends the low acceptance band regardless of wiring order. Lowest now selects 0.1 s / 30 s; highest selects 1 min / 1 h. The decoder supports any contact order, with nearest-band selection and invalid/open/unused contacts retaining the prior setting. See the [fresh calibration measurements](test/dial_calibration_report.json) and [PCB pin mapping](docs/PCB_PIN_MAP.md). Future calibration can use the wireless diagnostics without USB. GPIO34/35 are ADC1 inputs, so they work with Wi-Fi enabled. Display wiring, matrix mapping and chain order follow the working demo.

## Program the ESP32

Install PlatformIO, open this project, and build:

```sh
pio run
pio device list
pio run -t upload --upload-port YOUR_ESP32_PORT
pio device monitor --port YOUR_ESP32_PORT --baud 115200
```

The first USB upload installs the bootloader, dual-slot partition table and firmware. Future OTA transfers update only the application slot. Select the ESP32 port explicitly; do not flash another attached device. This firmware targets a 4 MiB WROOM-32E, with GPIO16/17 available for the Nixies.

## Connect Wi-Fi

At first boot, connect your phone/computer to `BitcoinClock-xxxxxx` **without a password**, then open `http://192.168.4.1` if the setup page does not appear automatically. Choose your network from the scanned list, sorted by signal strength. Duplicate network names appear once, and open networks connect without a password. Secured networks ask only for their Wi-Fi password. Use **Scan again** to refresh the list or **Other network…** for a hidden/unlisted SSID. The ESP32 uses the 2.4 GHz part of a mixed-band network; splitting your router's network names is normally unnecessary. Upgrading from an older release removes the old setup hotspot password while preserving saved home Wi-Fi credentials.

For a secured network, enter its Wi-Fi password. Credentials are saved as one atomic NVS record only after a successful connection. Eight seconds later the setup web server, captive DNS and access point stop. Subsequent boots reconnect automatically. A failed saved connection opens setup; an established connection that is lost is retried, with setup reopening after two minutes.

The matrix shows `CONNECT` above `WIFI` while waiting for setup. A single zero runs across the six Nixies at 100 ms per tube; the previous tube turns off first. During setup the matrix reports `CONNECTING`, `CONNECTED`, or a Wi-Fi/save/AP error. Long words scroll to fit the 21-column display. In 1.0.10 the zero chase continues through clock sync and first-price loading. The first fresh, validated quote replaces it immediately; independent confirmation proceeds in the background. Direct USD HTTP fallback starts as soon as the clock is synchronized, before stream handshakes and without fetching USDT conversion first. After setup closes, the startup chart reveal runs when the first real candle or saved chart is ready. Clock synchronization, initial price fetching and initial history fetching also have text status messages.

Hold the ESP32 BOOT button for five seconds while running to reopen setup. Hold for one second and release to toggle matrix diagnostics; short taps cycle readings/counters/IP/tube test. Diagnostics are also available at `http://bitcoin-clock.local:8080`. Serial commands at 115200 baud include `status`, `dials`, `ticks`, `tube-test`, `cadence-test`, `readings`, `ota-check`, `setup`, `wifi-reset`, `selftest` and `reboot`. `wifi-reset` removes saved Wi-Fi credentials. `selftest` lights the six LED grids in order. An X indicates that no usable chart has arrived after a price is available. The standalone all-LED demo is preserved in `demo/matrix_all_on/`; its separate PlatformIO configuration cannot replace the live build artifacts.

Prices require an SNTP-synchronized clock and certificate-verified HTTPS. Nixies blank after an extended quote outage, instead of presenting an old value as current. Values that would round beyond 999999 also blank, because six tubes cannot represent seven digits. A chart can retain dated historical points during an outage.

## Automatic updates from main

The public repository is [jeebot678/bitcoin-nixie-clock](https://github.com/jeebot678/bitcoin-nixie-clock). Devices check [the main-branch rollout manifest](https://raw.githubusercontent.com/jeebot678/bitcoin-nixie-clock/main/ota/manifest.json) about 60–90 seconds after boot when connected, then every six hours with up to fifteen minutes of jitter. `include/OtaConfig.h` fixes the target branch to `main`.

An ordinary source commit does not install anything on a device. A new, signed `ota/manifest.json` on `main` selects a versioned firmware release asset. The manifest binds version, hardware, image size, SHA-256 and URL with an RSA-2048/SHA-256 signature. Firmware verifies HTTPS, signature, newer version, target board, slot size and complete downloaded image hash before activating the inactive OTA slot. Download failure retains the current application. A newly booted image must establish healthy operation within 90 seconds or request rollback; a crashed image can also be rolled back by the bootloader. A failed version is retried at most once per day.

Keep and back up `secrets/ota-signing-key.pem` privately. Only `data/cert/ota-public.pem` is published or embedded. Replacing the signing key requires a planned trust-key migration or a USB flash. The updater also retains the highest accepted version to reject downgrades after a rollback.

To publish a future update from a clean `main` checkout after committing your code changes:

```sh
python3 tools/publish_release.py --version 1.0.19
```

That command builds, runs the host tests, signs locally, pushes the versioned source, publishes the firmware release, then pushes the signed rollout manifest to `main`. The signing key is never sent to GitHub. Firmware 1.0.13 exposes OTA stages, HTTP status, candidate version, transfer progress and retry delay through wireless diagnostics. Failed transfers that never activate an image can be retried immediately; activated images retain the one-day rollback retry protection. HTTP work releases the secondary feed first. Ordinary quote/FX work defers instead of closing the primary if its TLS/JSON reservation cannot fit. Infrequent OHLC bootstrap can pause the primary to reserve TLS memory; OTA can also pause both feeds. Both feeds pause for a validated new image download/install and resume on failure. The display timer and rotary inputs continue running.

## Verification

```sh
pio run
python3 tools/run_tests.py
```

The independent live-price simulation runs on the computer, without an ESP32 or npm dependencies. Use Node with built-in `WebSocket` support (tested on Node 25.9.0):

```sh
node tools/simulate_prices.mjs --mode stream --policy fast --seconds 180
```

It probes the eleven firmware REST providers and Bybit, subscribes to eight public trade streams, validates real quotes, and records every response and display sample. A three-minute run delivered 28.1 accepted quotes/s with a longest quote gap of 274 ms and a new quote at every 400 ms sample, without rate-limit responses. See the [benchmark, endpoint rules and reproduction commands](docs/PRICE_REFRESH_BENCHMARK.md). That benchmark uses eight simultaneous computer-side streams. Firmware 1.0.9 uses two independent USD streams, per-provider quote caches, provider failover and REST fallback, plus a dial-controlled Nixie timer. Version 1.0.18 retains twelve distinct real quotes per provider for at most five seconds and prefers changes in multiple whole-dollar digits. If all available quotes round alike it requests another provider in the background while preserving the display deadline. It never manufactures a larger price movement. Chart/history values remain real. Its physical USB measurement is recorded separately in [the device refresh report](test/device_refresh_report.json); reproduce it with `tools/verify_device_refresh.py` as described in [refresh validation](docs/NIXIE_REFRESH.md).

The host tests compile the actual provisioning, display, application-loop, HTTP worker, TLS bundle callback and OTA code with modeled hardware/network/flash and address/undefined-behavior sanitizers. They cover all eleven live quote fixtures, both history providers at five scales and their ESP32 memory budgets, truncated/stale HTTP responses, dial noise/debounce, rollover, currency conversion, stale data, Nixie packet bytes, setup text/scrolling and single-zero chase timing, LED chain ordering, Wi-Fi errors/reconnection/persistence, OTA signatures/corruption/interruption/flash failure, and boot rollback decisions. Startup tests check the physical SPI output at every fill and clear step, preservation of chart pixels, changes to the chart during the reveal, delayed frames, once-per-boot behavior and clock rollover. The portal's actual JavaScript runs against a DOM/network model to check selection, safe SSID rendering, scanning/retries, passwords, submission and connection status. TLS tests require exact trusted-root subjects and public keys, verify real ECDSA signatures and preserve certificate validation failures. The candle suite verifies chronological OHLC, fixed boundaries, stable heights, controlled panning, spike expansion, 30-minute smoothing, exact scale/candle restoration, all corrupted bytes and interrupted writes, real trade fixtures and physical SPI masks. The retained legacy history-store code is tested for restoration of every range, two-day incremental backfill, internal gaps, duplicates, expired data, every truncated-write/byte-corruption offset, failed storage and generation rollover. Runtime tests verify instant range recall, no redundant downloads/writes and saved charts before SNTP. A two-hour simulated runtime crosses the `millis()` wrap and injects rate-limit failures. OpenSSL independently supplies the host crypto adapter; ESP32 builds use mbedTLS. CI repeats the build and host suites on `main` and pull requests.

`src/CertificateBundle.cpp` adapts the Apache-licensed Arduino-ESP32 bundle verifier so a cross-signed root can terminate a chain when its complete subject and public key match the embedded Mozilla trust store. Certificate dates, hostnames and other verification failures remain enforced. Chart JSON pools are sized for the selected window and reserved before the HTTPS handshake to avoid heap fragmentation.

`test/live_probe_report.json` records low-volume endpoint verification, status and response headers. `test/price_benchmark_report.json` records the bounded computer-side throughput runs; these are separate from device acceptance. `docs/SOURCES.md` records official quota and freshness documentation. Firmware 1.0.9 has been USB-flashed with esptool hash verification and physically checked for live reception, 500 ms frame cadence, saved history and the OTA manifest check without watchdog restarts. Firmware 1.0.7 was withdrawn after device TLS watchdog failures. The fresh all-detent voltage sweep is recorded separately; applying its calibration to the assembled device, full-size 24-hour simultaneous TLS memory use, visible tube/LED appearance and actual power-loss/bootloader rollback still require the device acceptance checks in `docs/DEVICE_ACCEPTANCE.md`.

After publishing, `python3 tools/verify_deployment.py` independently verifies the public `main` manifest signature and downloaded release image, using the firmware's HTTP mode, redirect hosts and Mozilla certificate roots. `python3 tools/probe_sources.py --history` refreshes the public market fixtures with five-second spacing between requests to the same provider; the excluded Bybit endpoint remains a geographic-access diagnostic.
