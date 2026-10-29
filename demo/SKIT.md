# Video skit demo

Build with `pio run -e skit-demo`. Live firmware remains the default environment.
The demo starts automatically at boot and loops every 75 seconds, independently of Internet/market data.

- 0–15 seconds: approximately $81,000 with small fluctuations and a pre-seeded, gently varying chart.
- 15–35 seconds: sharply accelerating rise with irregular surges and pullbacks to $1,050,000; tubes saturate at 999999.
- 35–45 seconds: the chart continues climbing exponentially toward $2,337,000 with variation; tubes stay capped at 999999.
- 45–47 seconds: approximately $10,000.
- 47–49 seconds: approximately $1,000.
- 49–51 seconds: approximately $100.
- 51–53 seconds: $5.
- 53–75 seconds: holds at $1.
- At 75 seconds: automatically restarts the sequence and resets the chart.

The tubes refresh every 500 ms and the candle chart is locked to 30 seconds,
regardless of dial positions. Short-tap BOOT to replay; power cycling also replays.
The chart uses the simulated uncapped price. Its opening scale keeps candles modest;
as the spike grows, the opening variation compresses toward a flat line. The rally anchors earlier prices at the bottom and expands a linear axis to place
the current observed high one row below the top, including after the tubes saturate. Adjacent candle edges are joined without changing OHLC.
After the crash the axis fits visible OHLC highs/lows and recovers detail as old peaks age out. No simulated history is saved.
Saved Wi-Fi, signed OTA checks, remote OTA commands and boot validation remain.
Telemetry includes `demo: "skit"` and marks the display as estimated.

Publish with `python3 tools/publish_release.py --version NEXT --environment skit-demo`.
A subsequent higher-version default build restores live operation through OTA.
Timing and curve constants are in `include/SkitDemo.h`.
