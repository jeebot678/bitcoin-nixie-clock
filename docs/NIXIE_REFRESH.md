# Nixie refresh and chart behavior

Firmware 1.0.27 keeps the selected display cadence independent of network
requests and shows a different tube number at every selected deadline after
the first actual quote, using a bounded display estimate when needed.
Frequency settings are 100 / 500 / 1500 / 10000 / 60000 ms; range settings are
30 / 60 / 300 / 1800 / 3600 seconds. Each deadline writes all six Nixie modules,
preferring an unadjusted eligible actual quote. When real quotes cannot supply
a different number, it selects a display estimate identified in diagnostics even if requests
are delayed, the actual price is stale or Wi-Fi is offline. Before the first
actual quote, the setup animation remains active. An explicit tube diagnostic
temporarily overrides the normal display path.

The quote book retains up to twelve distinct rounded prices per provider for
five seconds. Older book midpoints and trade quotes retain this five-second
limit. Only the latest direct or converted midpoint of synchronized, valid
and healthy live books can remain selectable beyond it. Repeated receipts refresh the current
entry rather than evicting its alternatives. Quotes can be reused within
that lifetime, or while the latest live book remains current, so a display
change does not mean a newly executed trade.

Version 1.0.25 remembers the last eight changes of rounded whole-dollar
display values. A return from B to the preceding distinct value A is
ineligible until a third value has been displayed. An A → B → B → A sequence
is therefore blocked just like A → B → A: unchanged frames, decimal-only
updates and provider changes do not erase the preceding distinct value.
This hard rule applies both to actual quote selection and the display estimate
fallback, so the fallback cannot reintroduce an A/B loop.

Among eligible quotes, selection first favors unseen values or those least
recently displayed, then the greatest number of changed digits. An
`esp_random()`-derived tie score selects between remaining distinct rounded
prices. The score depends on the dollar value, so duplicate entries or
multiple providers reporting the same rounded number do not add weight.
Randomness in actual-quote selection chooses an existing validated value.
Version 1.0.26 separately permits the estimate fallback described below.
Original quote timestamps, receipt ages, five-second TTLs and the current-book
exception are unchanged. Display selection does not alter candle
observations, range settings or scale parameters.

The user-authorized estimate fallback replaces normal-operation holds and
stale/offline blanking after the first actual price arrives. Its base is an
unaltered real quote, with a dollar adjustment bound of
`clamp(round(real price × 0.0001), 2, 10)`. A random candidate within that
bound cannot equal the latest displayed number or the preceding distinct
number. Candidates changing at least two digits are preferred wherever the
bounded set permits them. An eligible actual quote that supplies a different
number remains preferred with ordinary nearest-dollar rounding.

An estimate is a display effect around the retained real base, not a new
market observation or prediction of the next trade. It cannot enter the
quote cache, guard, actual source/accepted-price counters, canonical OHLC,
volatility calculations or flash history. The real base's source, price,
publication timestamp and receipt time remain intact, and their age keeps
advancing throughout a stale/offline period. The normal timer continues to
write changed numbers at 100 / 500 / 1500 / 10000 / 60000 ms without renewing
the underlying market quote's TTL. Market reception and REST recovery run
independently, with existing provider cooldowns and TLS protections.

Version 1.0.27 does not request another REST quote solely because real prices
remain unchanged while any live feed is fresh. The display estimate supplies
visual motion without reclaiming a healthy secondary stream for unnecessary
HTTP. Required independent confirmation and genuinely stale feeds can still
request real quotes; history bootstrap and FX work retain their own schedules.
The earlier five-second sustained-sameness HTTP policy was used by prior
versions and is no longer the current fresh-feed policy.

Coinbase is the preferred primary USD stream. Version 1.0.23 introduced Bitfinex
realtime depth-one book midpoints on the secondary to supply genuine venue
price diversity. The midpoint is the average of the current best bid and
ask, not an invented price or an executed trade. TIMESTAMP publication
milliseconds establish publication freshness when a snapshot or level update
arrives. The ordinary Bitfinex ticker proved too
slow in the separate probe and is excluded. Kraken remains a fallback. Kraken adds matched-trade batches and USDT/USD conversion on
its existing TLS connection. Binance and Gemini join Bitstamp and Bitfinex
as stream fallbacks. Both workers retain 16 actual ticks each; a burst no
longer overwrites every intermediate price before the main loop can inspect
it. The main loop drains up to 32 ticks with a four-millisecond work budget
so burst processing does not delay the display timer. FX-only ticks update
conversion without counting as Bitcoin prices or masking stale Bitcoin data.
See [verified endpoints, public quotas and payloads](SOURCES.md).

Version 1.0.25 subscribes to three `P0/F0`, depth-one Bitfinex books on that
same TLS connection: `tBTCUSD`, `tBTCUST` and `tUSTUSD`. `UST` is USDT in
these symbols. The direct quote is the BTC/USD best-bid/ask midpoint. The
additional converted USD quote multiplies the BTC/USDT midpoint by the
USDT/USD midpoint. Both conversion books must have acknowledged, distinct
channel IDs, valid snapshots and levels, acceptable spreads, no corruption,
and their own channel message or heartbeat within 35 seconds. Both FX book
levels must remain within 0.5–1.5 USD per USDT. Coinbase and all three
Bitfinex subscriptions still occupy only two TLS connections.

A converted quote is emitted only when an actual level publication is
processed on either conversion leg. Its publication timestamp is the
later of the two actual leg publication times; its receipt is the time that
real event was processed. Both books retain their own publication times.
Channel heartbeats produce no quote, accepted-price count or fresh receipt.
The main loop accepts a validated converted midpoint for tube selection and
price counters, but never adds it to canonical chart OHLC, alters range
caches or treats it as an executed BTC/USD trade.

The selector retains separate current direct-USD and converted-USD heads
alongside its historical ring. These two records use 48 bytes, and updates
on one product cannot evict the other quiet, valid product. Historic ring
entries keep the ordinary five-second TTL. Each current head retains the
original publication and receipt timestamps; separate validity masks govern
the direct book and both conversion legs.

Version 1.0.24 introduced eligibility for an unchanged latest midpoint;
1.0.25 extends it to the converted head when both legs remain valid.
Current-book validity requires the acknowledged matching `P0/F0`, depth-one
channel, a completed snapshot with both valid levels, no corruption, a
non-crossed spread of at most 1%, an established connection, and that
channel's own message or book heartbeat within 35 seconds. Neither unrelated
book traffic nor a transport PONG renews a channel's health. New level publications still pass
timestamp validation; heartbeats do not manufacture a new publication.
Removal of a required level, invalid data, a subscription reset, corruption,
disconnect or missed message-health deadline removes eligibility. Historical
entries never gain this exemption: only the latest retained direct or
converted BBO midpoint for its valid current books can outlive the receipt-age limit.

The selector preserves the original publication and receipt timestamps when
reusing a current midpoint. Heartbeats update transport health without
refreshing quote timestamps, incrementing accepted-price counters or adding
chart candles. A healthy quiet book therefore does not trigger REST merely
because its latest level update is older than five seconds. Independent
confirmation and a genuinely stale or invalid feed can still request REST
when needed; unchanged live prices alone no longer trigger it.

HTTP dispatch holds new handshakes but preserves established subscriptions.
Only a real data-RAM reservation can release the secondary. Routine quote/FX
requests defer rather than close the primary. OTA and infrequent candle
bootstrap jobs may pause it when necessary. After HTTP work, missing feeds
receive a bounded 35-second reconnect grace, including a missing secondary
while the primary remains healthy. TLS still reserves two separate 18 KiB
record allocations plus 24 KiB context/crypto headroom under the shared gate.
This fixes the pattern where requests for a different price repeatedly tore
down the second stream and left a sparse fallback as the only live source.

The initial OTA gate now uses a separate fixed 35-second grace set when
stream startup begins. Later HTTP jobs may extend reconnect warmup, but
cannot move that initial OTA deadline. The scheduled 60–90-second boot check
can therefore become eligible without waiting for repeatedly renewed HTTP
grace. Network availability, TLS memory and existing in-flight work still
control when the check can execute.

Version 1.0.21 arms the heartbeat timer only after the WebSocket upgrade
completes. A delayed PONG does not disconnect a feed that is delivering
valid messages; the independent 35-second message and 120-second Bitcoin
quote deadlines still detect dead or silent connections. A synchronized
current book is exempt from the quote deadline while its message health and
levels remain valid. Negative TLS
connection failures release the handshake gate immediately and retain their
error code for LAN diagnostics. A fallback can retry a preferred USD feed
after 30 seconds when another feed remains established, with exclusive
provider and recovery ownership preserving the other connection.

Version 1.0.27 explicitly sets task priority 0 immediately before every
disconnected socket's handshake loop. A fallback can close and replace an
online socket earlier in the same worker step; previously, the step could
retain priority 1 and run preferred-feed TLS crypto before the next worker
iteration corrected it. That path could starve core 0's idle watchdog task.
The new handshake boundary resets priority even during such same-step
recovery, while established socket reads retain priority 1.

The matrix has 21 fixed UTC candles and 13 rows. Body and wick pixels are
fully on or off, without pulse dimming. Each displayed column has at least
one lit cell once a price is available. An observed candle includes its
open-to-close body and high-to-low wick; an unobserved column displays a
flat close hold without adding synthetic trades to its stored OHLC data.
The chart keeps one consistent venue across all five ranges, independent
of the selected view and Nixie frequency. Coinbase takes priority, then
Kraken; other trusted providers can fill in during an outage. A current
canonical source stays selected while connected and quiet for up to
30 seconds. A displayed book midpoint cannot replace recent canonical
observations for that interval, so a venue premium does not inflate chart
volatility. A returning Coinbase quote restores priority immediately. Bootstrap responses
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
`current_books_mask` identifies current direct-USD books;
`current_converted_books_mask` identifies current converted-book quotes.
Bitfinex uses bit 4 (`16`) in each. `display_quote_kind` distinguishes
`last-trade`, `bbo-midpoint` and `converted-bbo-midpoint`.
`display_quote_age_ms` reports the actual base quote's receipt age,
even when it exceeds five seconds for a current book or an outage estimate.
An advancing age with unchanged source quote counts is expected during a
healthy quiet book and does not indicate a fabricated price receipt.
`display_estimated` identifies a fallback estimate;
`display_adjustment_usd` gives the displayed difference from the real base,
including ordinary dollar rounding when no estimate is used. The boolean
marker therefore distinguishes the fallback from rounding;
`estimated_frames` counts these display frames independently of actual
accepted quotes. `display_quote_kind` continues to describe the underlying
actual quote, rather than relabeling an estimate as a trade or book update.
They also report HTTP kind/source/status, a requested differing quote, cached
candle count/epoch, chart columns/pixels, usable data-RAM and frame timing.
`reset_reason` and `reset_reason_code` identify the ESP32's reported cause of
the current boot, enabling separate analysis of reboot segments.
Per-deadline transition, changed-price and multi-digit-change counters observe
every 100 ms frame even when LAN collection is slower. `price_reversals` and
`selection_holds` distinguish rounded display reversals and unchanged frames.
After a real quote arrives, the normal 1.0.27 path should produce neither
reversals nor holds, while `estimated_frames` distinguishes its display
effects from newly received real prices. The 30-second
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
1.0.22 then added a five-second sustained-sameness holdoff; 1.0.27 removes
sameness-only requests altogether while a live feed remains fresh.

The 1.0.22 device run kept both feeds connected, received 12.69 quotes/s,
and changed 1,797 of 1,798 100 ms transitions (99.94%). Only 863 transitions
changed multiple digits (48%); the two venues often occupied the same
ten-dollar band. Its [report](../test/device_release_1.0.22_report.json)
retains this unmet variation target. Bitfinex book diversity and consistent
chart venue selection address that remaining requirement in 1.0.23.

The 1.0.23 device was installed by signed main-branch OTA and boot-confirmed.
Its continuous collector measured 333.029 seconds of steady operation after
the scheduled startup update check: 3,330 frames averaged 100.009 ms; all
3,330 transitions changed the price and multiple digits. Coinbase plus
Bitfinex stayed connected, receiving 2,985 prices (8.96/s), without failures,
queue drops or rejected messages. All 21 matrix columns remained displayed;
the chart stayed on Coinbase while the Nixies selected both actual trades
and genuine book midpoints. Flash checkpoints advanced, minimum usable
data heap was 13,812 bytes, and no synthetic price offsets were used.
See [the passing device report](../test/device_release_1.0.23_report.json).

A later 1.0.23 quiet-book interval exposed a failure outside that passing
sample: the unchanged Bitfinex midpoint expired from selection after five
seconds despite a healthy synchronized connection. Losing that current
alternative triggered unnecessary REST work. The recorded 333-second pass
is retained for its actual conditions; it does not establish correct behavior
for the later quiet-book case. Version 1.0.24 fixes current-book eligibility
without extending old book entries or trades, rewriting their ages or
inventing new quotes. The signed 1.0.24 OTA installation and boot were verified. Its complete
ten-minute steady observation measured 5,996 frames and 5,996 genuine
multi-digit transitions, averaging 99.994 ms per frame. Both preferred feeds
stayed connected, with zero errors, disconnects, queue drops, rejections or
quote HTTP requests. They supplied 4,369 incoming prices (7.29/s); display
selection can reuse recent observations, so these are distinct from the
5,996 rendered transitions. The current-book exception was exercised in
seven samples with receipt ages over five seconds, reaching 7.578 seconds
without rewriting the original quote times.

All 21 chart columns remained displayed, the chart stayed on Coinbase,
checkpoints advanced, and usable data heap remained at least 11,980 bytes.
The cumulative frame-gap bounds since initial period setup were 82–114 ms;
the counter deltas and average cover the complete steady observation. See
[the passing v1.0.24 report](../test/device_release_1.0.24_report.json). The
[separate startup record](../test/device_release_1.0.24_startup_report.json)
retains initial feed acquisition and the scheduled OTA memory reservation,
including its temporary reduction in fresh-price variation. Startup and
network interruption limits remain as described above. Visible-output
verification remains false; telemetry records actual firmware commands.

The scheduled startup OTA manifest check did temporarily pause both feeds
for its conservative TLS reservation. This is retained separately in
[the startup report](../test/device_release_1.0.23_startup_report.json); its
100 ms command timer continued, but fresh-price changes paused until feeds
reconnected. Steady performance is not a claim of uninterrupted market
reception during startup, firmware checks, network outages or installation.
These reports measure commands and firmware state, not visible cathode light.

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

Earlier passing change-rate reports do not test the distinct-change rule:
alternating two genuine venue prices can raise both changed-price and
multi-digit counters on every frame. Version 1.0.25 specifically prevented
that return to the preceding distinct value and selected less recently used
eligible prices before comparing digit changes. Its subsequent 180-second
steady device baseline recorded 1,801 changed transitions out of 1,801,
averaging 100.021 ms, with zero holds or reversals and both preferred feeds
healthy. See [the v1.0.25 device report](../test/device_release_1.0.25_report.json).
That run preceded display estimates and used actual received quotes.

The separate 90-second computer probe using the original Coinbase and direct
Bitfinex pair ran the actual compiled C++ parser, guard and selector with
modeled reception/frame fallback. After warmup, 575 of 788 transitions
changed the rounded value (73%), with 378 multi-digit changes, 213 holds and
zero distinct-value A → B → A reversals. Every displayed price was traceable
to a real accepted quote. See [the two-source report](../test/anti_repeat_quote_probe_report.json).
This shows why a third real quote is useful when the reversal rule blocks
two-price oscillation. Its computer timing and modeled frame orchestration
remain distinct from the assembled-device baseline above and visible output.

The final 60-second three-book computer run passed with the actual compiled
C++ parsers, guard and selector. After ten seconds of warmup, all 500
transitions changed the rounded price; 444 changed multiple digits (88.8%),
with zero reversals, holds or blanks. Frame gaps averaged 100.002 ms and
ranged from 97 to 103 ms. Both TLS connections stayed established without
errors and supplied 392 Coinbase trades, 172 direct USD book quotes and 40
converted book quotes. All 597 nonblank frames, including startup, were
traceable to an accepted real value with exact nearest-dollar rounding.
There were three startup blank frames before usable data arrived.

The current converted-head exception was exercised in 17 frames whose real
receipt age exceeded five seconds, reaching 14.491 seconds. Heartbeats did
not create new quotes or reset those ages. See [the three-book computer report](../test/anti_repeat_three_books_probe_report.json).
The full host suite and ESP32 build passed for that pre-estimate version.
This probe models reception and frame orchestration on the computer; it is
separate from the assembled-device baseline. Actual application/SPI host cases
cover stale/offline conditions at all five refresh settings, estimate bounds,
original quote ages and unchanged real-data counters/history.

The 1.0.26 live acceptance run is retained as failed after an unexplained
reboot around 164 seconds. See [its report with separate boot segments](../test/device_release_1.0.26_report.json).
Usable data heap reached 2,052 bytes earlier, around 52 seconds; that reading
does not establish the cause of the later reboot. Manual restart remains
possible, and the old firmware did not expose a reset-reason field.

Review established a separate same-step TLS priority defect, corrected in
1.0.27 as described above. It is not proven to have caused the observed
1.0.26 reboot. Fresh-feed sameness-only REST and the extendable initial OTA
gate are also corrected, and reset-reason diagnostics support the next
measurement. Version 1.0.27 installation and device acceptance remain pending.
Visible appearance is not established by telemetry.

The prior 1.0.15 bare-ESP32 measurement passed its 30-second diagnostic but did
not exercise ongoing minute-history requests on the assembled 5-minute range.
It is preserved in [that release report](../test/device_release_1.0.15_report.json).
The new starvation regression checks the missing coverage explicitly.

Run `python3 tools/run_tests.py` and `pio run`. Tests exercise actual firmware
sources with modeled network/hardware, address/undefined-behavior sanitizers,
all five refresh intervals, real-price selection and duplicate/stale handling,
distinct-change reversal rules across holds and decimal/provider updates,
tie selection without duplicate weighting, display-only estimate bounds,
continued stale/offline cadence after the first quote, estimate telemetry,
unchanged actual quote ages/counters and no synthetic OHLC/history,
quiet synchronized books with unchanged timestamps/counters, removal and
corruption invalidation, message-health expiry and historical quote TTLs,
separate direct/converted heads, both-leg conversion validity and independent
channel health, original leg timestamps and heartbeat silence,
stream burst preservation, FX-only freshness, fallback schemas/backoff, HTTP
reconnect grace and memory deferral, no sameness-only REST with fresh feeds,
same-step preferred recovery priority, fixed initial OTA grace and reset-reason
telemetry, bounded transaction parsing, fixed OHLC
candles, stable scales, full-brightness pixels and close holds, saved
candles/backfill, range switching, OTA and TLS checks.
LAN timing and pixels measure firmware commands; visible tubes/matrix still
require inspection because eXixe provides no MISO/cathode feedback.

The preserved protocol, CS pins and matrix mapping follow the working demo.
See [the manufacturer protocol](https://github.com/dekuNukem/exixe/blob/master/technical_details.md),
[PCB mapping](PCB_PIN_MAP.md), [endpoint documentation](SOURCES.md), and
[wireless collection/update commands](WIRELESS_DIAGNOSTICS.md).
