# Static matrix demo

Holds all 273 physical matrix LEDs on at the normal configured brightness. The Nixie tubes are blanked once at startup. There is no Wi-Fi, price polling, animation, dial handling or OTA. The live app and its default PlatformIO configuration remain intact.

Find the ESP32's current USB port with `pio device list`, then upload the demo:

```sh
pio run --project-conf platformio.matrix-demo.ini -e matrix-all-on -t upload --upload-port YOUR_ESP32_PORT
```

Restore the live app later:

```sh
pio run -e esp32-devkitc-32e -t upload --upload-port YOUR_ESP32_PORT
```

Both use the same partition table and preserve saved Wi-Fi credentials. A source archive and verified live firmware are also saved locally in the ignored `release/matrix-demo-restore/` directory. The demo uses a separate build directory so it does not replace the saved live build.
