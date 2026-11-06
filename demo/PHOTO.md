# Static photo demo

Build with `pio run -e photo-demo`.

The tubes show **456081** and the matrix holds a static, sharply rising candle
chart. Both displays are written once at startup and remain unchanged: no
scrolling, price ticks, reveal animation, replay or dial effects. It works without
Internet access. Saved Wi-Fi, telemetry and signed OTA remain available for future
updates. The live and looping skit build environments remain available separately.

Chart composition and the fixed price are in `include/PhotoDemo.h`.
