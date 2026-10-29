# Video skit demo

Build with `pio run -e skit-demo`. Live firmware remains the default environment.
The demo starts automatically at boot and loops every 60 seconds, independently of Internet/market data.

- 0–15 seconds: approximately $81,000 with small fluctuations.
- 15–35 seconds: accelerating rise to $1,050,000; tubes saturate at 999999.
- 35–45 seconds: peak hold for the reaction.
- 45–48 seconds: sharp crash to $1.
- 48–60 seconds: holds at $1.
- At 60 seconds: automatically restarts the sequence and resets the chart.

The tubes refresh every 500 ms and the candle chart is locked to one minute,
regardless of dial positions. Short-tap BOOT to replay; power cycling also replays.
The chart uses the simulated uncapped price. No simulated history is saved.
Saved Wi-Fi, signed OTA checks, remote OTA commands and boot validation remain.
Telemetry includes `demo: "skit"` and marks the display as estimated.

Publish with `python3 tools/publish_release.py --version NEXT --environment skit-demo`.
A subsequent higher-version default build restores live operation through OTA.
Timing and curve constants are in `include/SkitDemo.h`.
