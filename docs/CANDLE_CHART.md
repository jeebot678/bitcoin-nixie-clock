# Candle matrix and stable scale

Firmware 1.0.19 uses the physical 21×13 matrix for 21 OHLC candles. Each
column holds an open-to-close body and a high-to-low wick. Bodies stay lit;
wick-only pixels use a 1/3-duty, 12 ms cycle on the monochrome matrix. The
startup fill/unfill preserves the complete body and wick mask, then resumes
normal wick dimming. There are no diagonal connectors between columns.

| Range | Duration per candle |
| --- | --- |
| 30 s | 30/21 s ≈ 1.429 s |
| 1 min | 60/21 s ≈ 2.857 s |
| 5 min | 300/21 s ≈ 14.286 s |
| 30 min | 1800/21 s ≈ 85.714 s |
| 1 h | 3600/21 s ≈ 171.429 s |

The bucket ID is `floor(UTC milliseconds × 21 / range milliseconds)`. Its
opening boundary is `ceil(bucket ID × range milliseconds / 21)`. Keeping
these rational boundaries avoids accumulating rounding errors or reshaping
closed candles every time the window advances. The rightmost candle is
forming; a new bucket scrolls the preceding candles one column left. The
visible window ends at the forming candle's scheduled close. Only observed
prices are aggregated. Empty buckets remain empty through outages.

Each candle stores earliest open, latest close, and every accepted high and
low, to USD-cent precision. Stream timestamps preserve milliseconds where
provided. Bitstamp history has whole-second timestamps; trade IDs decide
open and close among trades within the same second. History bootstrap uses
real individual trades rather than attempting to split one-minute candles
into shorter bars. Live candles aggregate validated USD/normalized-USD
quotes from the active sources; these are a composite observed-price chart,
not an exchange-specific trading chart. Provider spread can affect its wicks.

Each range has its own retained dollars-per-row and lower price bound.
Ordinary updates keep dollars-per-row fixed. The view can pan one whole row
every five UTC seconds toward the visible candle center, preserving candle
heights. A price more than two rows beyond the view, or a visible high-to-low
extent greater than 150% of the current span, triggers immediate widening
and inclusion of the extreme. Routine width adjustments use a 30-minute
exponential moving average of the padded visible price extent, with 15%
hysteresis and at most a 5% width change per minute. This is a display
volatility measure, not a 30-minute ATR indicator. Small moves can remain
clipped for several seconds while panning catches up; large spikes widen
immediately. After a spike the scale contracts gradually.

All five live ranges collect candles independently of the Nixie frequency
dial. The selected range bootstraps from Bitstamp minute/hour transactions;
Coinbase's newest 1,000 trades provide a bounded partial-history fallback.
A healthy continuous stream avoids repeated history downloads after
bootstrap. A reception gap resets coverage, allowing a later bootstrap to
repair real gaps. Public transaction APIs can return overlapping data; only
the relevant buckets are merged. They do not support arbitrary time-suffix
requests, and a Coinbase page may cover less than the selected range.

Onboard `/candles-RANGE-SLOT.bin` snapshots retain all OHLC values, exact
UTC snapshot time and scale parameters. Two alternating, CRC-covered
slots protect the prior snapshot during an interrupted write. Checkpoints
run at most every 30 seconds on the existing core-0 worker. Immutable copies
keep subsequent live ticks separate from in-flight writes. A checkpoint can
wait for ongoing network work. Powering off loses any data since the last
completed checkpoint; bootstrap repairs that gap. At boot
the saved chart is available before SNTP; synchronization then ages candles
against current UTC. Runtime scale timers reset on restart so time powered
off does not count as observed volatility. Previous close-only history files
remain intact and are never misrepresented as complete OHLC candles.

If there is insufficient data RAM for a bootstrap TLS connection, this
infrequent history job may temporarily pause the primary stream. Ordinary
quote/FX requests still preserve it. The Nixie display timer and rotary
sampling continue on the other core, and streams get reconnect grace after
the job. History failures respect provider cooldowns and retry/backoff.

`test/host/candle_tests.cpp` checks OHLC order, all five rational bucket
widths, closed-candle stability/scrolling, row-bounded panning, spikes,
30-minute scale response, exact cache/axis restoration, interrupted writes
and every corrupted byte, stale data, streaming real trades against an
independent reference, physical matrix register mapping and startup masks.
Runtime tests exercise actual cache scheduling, all five Nixie intervals,
reconnects and a two-hour run across timer rollover. These verify generated
commands; visible brightness and tube/matrix wiring require inspection of
the physical device.
