# Nixie refresh and chart timing in 1.0.9

The frequency selector controls a separate tube timer: 0.5 s, 2 s, 30 s,
5 min or 1 h. Every deadline sends all six modules their normal `0xAA`
brightness packets. Changing frequency starts an immediate frame; changing
range leaves the tube interval alone. Range is JSEL1/GPIO34; frequency is
JSEL2/GPIO35. An unread detent defaults to 0.5 s and is explicitly reported
as unconfirmed; this does not certify ADC calibration.

## Real quotes and visible motion

The main loop caches one accepted USD quote per exchange. At a deadline it
prefers a recent quote whose rounded dollars differ from the displayed digits,
rotating through providers instead of letting a busy feed overwrite the rest.
Quotes received within 1.5 seconds are preferred; alternatives expire after
5 seconds. Expired USDT conversion excludes USDT cache entries. Cached prices
outside 5% of the current confirmed market value cannot resurrect an old
price after a large move. Source receipt times are never rewritten by display
selection, and startup/large moves still require independent confirmation.

At **0.5 s only**, when genuine eligible quotes round to the same displayed
dollar, a cosmetic rendering effect uses the adjacent integer in the direction
of the raw price. Its error is at most **$1 from the selected real quote**.
An exact integer can use the next dollar. The next frame normally returns to
the actual rounded dollar. This is visual motion, not an exchange trade or a
fabricated input quote. Diagnostics retain the raw quote, actual displayed
digits and `cosmetic=yes/no`. Slower frequency positions use ordinary nearest
whole-dollar rounding. Cosmetic motion stops if the selected quote's receipt
age exceeds 5 seconds; ordinary outage blanking still applies. The chart and
flash history use real prices and timestamps, never cosmetic display values.

## Network workers and memory

Two independent TLS workers prefer Kraken BTC/USD `ticker` with the `bbo`
trigger and Coinbase BTC-USD `ticker`. Bitstamp BTC/USD trades and Bitfinex
`tBTCUSD` trade executions are fallback providers. Only one worker can own a
provider. Each has its own bounded latest-tick queue, so a burst on one cannot
remove a waiting quote from another. Exact symbols, timestamps, positive
finite values, Bitfinex subscription channel IDs and trade duplicates are
validated. A ticker heartbeat/acknowledgment never becomes a price.

At startup the two USD streams get up to 35 seconds to establish and confirm
each other before HTTP fallback can reclaim secondary memory. Confirmation
can finish sooner. The eleven REST providers remain available for independent
confirmation and fallback, at their existing conservative quotas. A live receipt within five
seconds suppresses redundant ticker/FX requests; USD streams do not need
USDT conversion. History, FX and OTA share the HTTP worker. Before HTTP work,
the secondary closes to release memory. The primary remains open unless a
large JSON pool actually needs its allocation as well. The 24-hour range
prefers Bitstamp's smaller JSON pool. No socket is touched from another worker.
The secondary resumes after the request completes.

A routine OTA manifest check retains the primary feed. Both streams pause
only after a newer signed manifest passes validation and the updater actually
needs to download/install its image. An unsuccessful pause does not mark the
version as attempted; download failure resumes reception.

New TLS handshakes share a lease through the WebSocket upgrade. Handshake
crypto and HTTP run at IDLE0's priority to keep the watchdog healthy;
established streams use priority 1 for responsive reads. TLS handshakes have a
20-second deadline. The stream workers each have 10 KiB stacks and HTTP has
16 KiB. Cooperative reads yield while waiting for bytes. The shared Mozilla
trust index is protected across connections; certificate signatures, dates
and hostnames remain enforced. No watchdog/certificate checks are disabled.
A 2 KiB UART transmit buffer prevents status reports from blocking tube timing.

A 60-second computer probe on 2026-10-11 received valid quotes from all four
USD streams. The actual C++ QuoteBook/PriceGuard code selected 100 post-warmup
frames at 500 ms, with 99/99 whole-dollar transitions changing **without the
cosmetic fallback**. Selected receipt age reached 4,110 ms. This validates
source availability/selection, not ESP32 timing. Repeat with
`node tools/probe_device_feeds.mjs`; see the separate
[computer feed report](../test/device_feed_probe_report.json).

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

Sanitized tests compile the actual runtime, SPI encoder, live workers, HTTP,
OTA and cache code. They cover all five frequencies, correct six-module
packets, bounded cosmetic rounding, unmodified market/chart values, source
rotation, per-provider cache expiry, independent queues, symbol/channel
binding, duplicate/stale messages, memory reservations, failure/backoff,
Wi-Fi/SNTP gating, OTA manifest versus installation pauses and millis rollover.
The physical-report tests reject perfect timers with stale quotes, unmoving
digits, missing sources, excess cosmetic error or a device fault.

All 21 chronological chart buckets at all five ranges, newest timestamp
selection, cached reboot restore and two-day incremental repair are exercised.
A two-hour modeled runtime crosses millis rollover with rate-limit failures.
The actual TLS callback has concurrent trust-lifetime/signature tests.

`ticks on` logs raw quote, actual digit value, source, cosmetic flag, interval,
frame gap and selected quote receipt age. `status` reports connected stream
count, advancing accepted quotes, heap and minimum remaining stream stack.
The final physical result and exact flashed image hash are recorded in
[the USB refresh report](../test/device_refresh_report.json).

Repeat with the assembled device at 0.5 s:

```sh
~/.platformio/penv/bin/python tools/verify_device_refresh.py \
  --port YOUR_ESP32_PORT --version 1.0.9 --warmup 60 --duration 180 \
  --max-quote-age-ms 5000 --minimum-sources 2 --minimum-motion-ratio 0.99
```

The 60-second startup phase is logged but excluded from steady measurements;
Wi-Fi, SNTP, TLS and independent-price confirmation can take time on a cold
boot. Device faults in either phase fail verification. The strict run requires
advancing counters, two actual displayed providers, the selected cadence,
quotes no more than five seconds old, at least 99% changing dollar transitions
and no frame more than 10 ms late. Cosmetic rendering is separately counted,
not reported as a new trade. All physical dial detents, visible tube/LED output,
full-size 24-hour concurrent TLS memory and power-loss/bootloader rollback
remain assembled-device acceptance checks.

Official endpoints and connection limits are linked in [SOURCES.md](SOURCES.md).
