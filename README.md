# Bitcoin Nixie Clock

Live, standalone firmware for the Rev20 motherboard: ESP32-WROOM-32E, six exixe Nixie modules, a 13 × 21 LED matrix driven by six MAX7219 chips, and two five-position resistor-ladder selectors. The original working simulation is archived in `demo/main_demo.cpp`; the default firmware uses actual market data.

The clock obtains BTC/USD from eleven independent, free public exchange endpoints. It rotates providers, honors provider cooldowns and `Retry-After`, and falls back when an endpoint is unavailable. USDT quotes are converted with a recent USDT/USD rate. No API keys, subscriptions, intermediary server or running web app are needed.

## Controls

| Position | Frequency: JSEL1 (GPIO34) | Range: JSEL2 (GPIO35) |
| --- | --- | --- |
| 1 | 0.5 seconds | 5 minutes |
| 2 | 2 seconds | 30 minutes |
| 3 | 30 seconds | 1 hour |
| 4 | 5 minutes | 24 hours |
| 5 | 30 minutes | 7 days |

The Nixies show the latest accepted price rounded to whole USD. The graph samples real closed candles into 21 time buckets and overlays the live tick. Changes below a pixel do not cause display traffic. Each visited range is saved in onboard flash, restored at boot and immediately available on later dial changes. Current prices are refreshed separately from historical candles. The fastest setting is a polling target; HTTPS latency, outages and firmware installation can extend the interval.

The caches retain actual UTC candle-close timestamps. After SNTP synchronization, the chart uses the current time, ages points out of the selected range and requests from the first missing candle. A two-day shutdown in the seven-day range reuses the remaining five days and requests the missing 48 hourly candles. Gaps stay empty until real data arrives. Turning the dial back to a complete cache makes no history request; the next request waits for a new candle. The three short ranges reuse their shared one-minute candles. Closed candles are independent of the price-frequency dial; 0.5 seconds is a request target, not a guaranteed network response time.

Snapshots use the existing 128 KiB SPIFFS partition, preserving Wi-Fi NVS and both OTA slots. Each range has two alternating, versioned files with checksums; incomplete writes fall back to the previous valid snapshot. Only changed closed-candle histories write to flash, never each live price tick. Normal USB application uploads and OTA preserve the caches; whole-chip erasure or filesystem formatting removes them. Failed storage writes retain RAM data and retry once per minute.

Without a battery-backed RTC, the ESP32 cannot know how long it was powered off until SNTP answers. Before synchronization, an available saved chart is shown at its recorded snapshot time; it is then immediately positioned against current UTC. Saved history never initializes the live price or the HTTPS clock. During Wi-Fi setup, the setup status still takes priority on the matrix.

Once a saved or freshly fetched chart is ready, the matrix performs a startup reveal once per boot: all 273 LEDs fill left to right, row by row from top to bottom, then clear in the same order while leaving the selected chart pixels lit. Each pass takes about 0.82 seconds, with a fully lit frame between them. The reveal follows current prices and dial selections and runs without delaying the main loop. Later range changes show their chart immediately without repeating it.

`include/DeviceConfig.h` contains independent calibration centers for each dial, debounce, intervals and brightness. The archived board documentation described six resistor positions; this application uses the first five (0, 503, 926, 1336, 1800 mV). The unused sixth contact and open-switch transitions retain the prior setting. **Measure your actual five selector positions with the serial `dials` command before device acceptance.** The pin assignments were checked against the linked PCB-design session and its Rev20 schematic/board; see [PCB pin mapping](docs/PCB_PIN_MAP.md). GPIO34/35 are ADC1 inputs, so they work with Wi-Fi enabled. Display wiring, matrix mapping and chain order follow the working demo.

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

The matrix shows `CONNECT` above `WIFI` while waiting for setup. A single zero runs across the six Nixies at 100 ms per tube; the previous tube turns off first. During setup the matrix reports `CONNECTING`, `CONNECTED`, or a Wi-Fi/save/AP error. Long words scroll to fit the 21-column display. The zero chase stops when Wi-Fi connects, and current prices replace it as they arrive. After setup closes, the startup chart reveal runs when the selected history is ready. Clock synchronization, initial price fetching and initial history fetching also have text status messages.

Hold the ESP32 BOOT button for five seconds while running to reopen setup. Serial commands at 115200 baud are `status`, `dials`, `setup`, `wifi-reset`, `selftest` and `reboot`. `wifi-reset` removes saved Wi-Fi credentials. `selftest` lights the six LED grids in order. An X indicates that no usable chart has arrived after a price is available. The standalone all-LED demo is preserved in `demo/matrix_all_on/`; its separate PlatformIO configuration cannot replace the live build artifacts.

Prices require an SNTP-synchronized clock and certificate-verified HTTPS. Nixies blank after an extended quote outage, instead of presenting an old value as current. Values that would round beyond 999999 also blank, because six tubes cannot represent seven digits. A chart can retain dated historical points during an outage.

## Automatic updates from main

The public repository is [jeebot678/bitcoin-nixie-clock](https://github.com/jeebot678/bitcoin-nixie-clock). Devices check [the main-branch rollout manifest](https://raw.githubusercontent.com/jeebot678/bitcoin-nixie-clock/main/ota/manifest.json) about 60–90 seconds after boot when connected, then every six hours with up to fifteen minutes of jitter. `include/OtaConfig.h` fixes the target branch to `main`.

An ordinary source commit does not install anything on a device. A new, signed `ota/manifest.json` on `main` selects a versioned firmware release asset. The manifest binds version, hardware, image size, SHA-256 and URL with an RSA-2048/SHA-256 signature. Firmware verifies HTTPS, signature, newer version, target board, slot size and complete downloaded image hash before activating the inactive OTA slot. Download failure retains the current application. A newly booted image must establish healthy operation within 90 seconds or request rollback; a crashed image can also be rolled back by the bootloader. A failed version is retried at most once per day.

Keep and back up `secrets/ota-signing-key.pem` privately. Only `data/cert/ota-public.pem` is published or embedded. Replacing the signing key requires a planned trust-key migration or a USB flash. The updater also retains the highest accepted version to reject downgrades after a rollback.

To publish a future update from a clean `main` checkout after committing your code changes:

```sh
python3 tools/publish_release.py --version 1.0.6
```

That command builds, runs the host tests, signs locally, pushes the versioned source, publishes the firmware release, then pushes the signed rollout manifest to `main`. The signing key is never sent to GitHub. Price polling pauses during the shared worker's update download; the loop and rotary inputs continue running.

## Verification

```sh
pio run
python3 tools/run_tests.py
```

The host tests compile the actual provisioning, display, application-loop, HTTP worker, TLS bundle callback and OTA code with modeled hardware/network/flash and address/undefined-behavior sanitizers. They cover all eleven live quote fixtures, both history providers at five scales and their ESP32 memory budgets, truncated/stale HTTP responses, dial noise/debounce, rollover, currency conversion, stale data, Nixie packet bytes, setup text/scrolling and single-zero chase timing, LED chain ordering, Wi-Fi errors/reconnection/persistence, OTA signatures/corruption/interruption/flash failure, and boot rollback decisions. Startup tests check the physical SPI output at every fill and clear step, preservation of chart pixels, changes to the chart during the reveal, delayed frames, once-per-boot behavior and clock rollover. The portal's actual JavaScript runs against a DOM/network model to check selection, safe SSID rendering, scanning/retries, passwords, submission and connection status. TLS tests require exact trusted-root subjects and public keys, verify real ECDSA signatures and preserve certificate validation failures. The actual history-store code is tested for restoration of every range, two-day incremental backfill, internal gaps, duplicates, expired data, every truncated-write/byte-corruption offset, failed storage and generation rollover. Runtime tests verify instant range recall, no redundant downloads/writes and saved charts before SNTP. A two-hour simulated runtime crosses the `millis()` wrap and injects rate-limit failures. OpenSSL independently supplies the host crypto adapter; ESP32 builds use mbedTLS. CI repeats the build and host suites on `main` and pull requests.

`src/CertificateBundle.cpp` adapts the Apache-licensed Arduino-ESP32 bundle verifier so a cross-signed root can terminate a chain when its complete subject and public key match the embedded Mozilla trust store. Certificate dates, hostnames and other verification failures remain enforced. Chart JSON pools are sized for the selected window and reserved before the HTTPS handshake to avoid heap fragmentation.

Endpoint probes are low-volume public requests, not load tests. `test/live_probe_report.json` records verification time, status and response headers. `docs/SOURCES.md` records official quota and freshness documentation. Earlier firmware has been USB-flashed to the attached ESP32 with esptool hash verification; v1.0.5 device acceptance is tracked separately. ADC calibration, real Wi-Fi/TLS memory use, visible tube/LED appearance and actual power-loss/bootloader rollback still require the device acceptance checks in `docs/DEVICE_ACCEPTANCE.md`.

After publishing, `python3 tools/verify_deployment.py` independently verifies the public `main` manifest signature and downloaded release image, using the firmware's HTTP mode, redirect hosts and Mozilla certificate roots. `python3 tools/probe_sources.py --history` refreshes the public market fixtures with five-second spacing between requests to the same provider; the excluded Bybit endpoint remains a geographic-access diagnostic.
