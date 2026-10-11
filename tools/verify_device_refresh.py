#!/usr/bin/env python3
"""Observe real firmware frames and live reception over an explicit USB port.

Requires pyserial (available in PlatformIO's Python environment). Does not reset
the device or change either dial, Wi-Fi credentials, history, or OTA settings.
Raw serial output stays in the ignored test/.build directory.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import statistics
import time

import serial

ROOT = pathlib.Path(__file__).resolve().parents[1]
FRAME = re.compile(r"Nixie frame at (\d+) ms: ([\d.]+) interval=(\d+) ms gap=(\d+) ms quoteAge=(\d+) ms")
STATE = re.compile(r"stream=(connected|offline) quotes=(\d+) accepted=(\d+)")


def summarize(text, duration, version, interval, minimum_frames, max_jitter=10):
    errors = []
    frames = [dict(at=int(a), price=float(p), interval=int(i), gap=int(g), quote_age=int(q))
              for a, p, i, g, q in FRAME.findall(text)]
    counts = [(state, int(quotes), int(accepted)) for state, quotes, accepted in STATE.findall(text)]
    if f"Firmware={version} " not in text:
        errors.append("Requested firmware version was not observed")
    if "Wi-Fi=connected" not in text or "clock=synced" not in text:
        errors.append("Connected Wi-Fi and synchronized clock were not observed")
    if any(fault in text for fault in ("Guru Meditation", "abort()", "Backtrace:", "task_wdt:")):
        errors.append("A device fault was observed")
    if len(frames) < minimum_frames:
        errors.append(f"Only {len(frames)} frames observed; need {minimum_frames}")
    if any(frame["interval"] != interval for frame in frames):
        errors.append("Frequency dial changed or does not match the requested interval")
    live_counts = [quotes for state, quotes, _ in counts if state == "connected"]
    if len(live_counts) < 2 or max(live_counts) <= min(live_counts):
        errors.append("A connected live stream with an advancing quote counter was not observed")
    accepted = [value for _, _, value in counts]
    if len(accepted) < 2 or max(accepted) <= min(accepted):
        errors.append("Accepted market quote counter did not advance")
    gaps = [b["gap"] for a, b in zip(frames, frames[1:]) if a["interval"] == b["interval"] == interval]
    if gaps and max(gaps) > interval + max_jitter:
        errors.append(f"A display frame missed its deadline by more than {max_jitter} ms")
    return {
        "checked_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "duration_seconds": duration, "firmware_version": version,
        "interval_ms": interval, "frames": len(frames), "measured_intervals": len(gaps),
        "allowed_jitter_ms": max_jitter,
        "gap_ms": {"min": min(gaps), "median": statistics.median(gaps), "max": max(gaps)} if gaps else None,
        "distinct_displayed_prices": len({frame["price"] for frame in frames}),
        "max_frame_quote_age_ms": max((frame["quote_age"] for frame in frames), default=None),
        "stream_quote_counter": {"first": live_counts[0], "last": live_counts[-1]} if live_counts else None,
        "accepted_quote_counter": {"first": accepted[0], "last": accepted[-1]} if accepted else None,
        "history_received": len(re.findall(r"closed candles received", text)),
        "history_saved": len(re.findall(r"History saved:", text)),
        "ota_check_completed": "OTA: firmware is current" in text,
        "frequency_selector_stable": "frequencyDial=stable" in text,
        "observed_ranges": sorted(set(re.findall(r"window=(.*?) price=", text))),
        "minimum_reported_heap_bytes": min((int(x) for x in re.findall(r" heap=(\d+)", text)), default=None),
        "minimum_reported_largest_allocation_bytes": min((int(x) for x in re.findall(r" largest=(\d+)", text)), default=None),
        "stream_stack_remaining_bytes": min((int(x) for x in re.findall(r"streamStackMin=(\d+) B", text)), default=None),
        "pass": not errors, "errors": errors,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--interval-ms", type=int, default=500)
    parser.add_argument("--duration", type=int, default=180)
    parser.add_argument("--minimum-frames", type=int, default=20)
    parser.add_argument("--max-jitter-ms", type=int, default=10)
    parser.add_argument("--output", type=pathlib.Path, default=ROOT / "test/.build/device-refresh")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = port.rts = False
    port.port = args.port
    port.open()
    start = time.monotonic()
    output = bytearray()
    pending = ""
    next_query = 1.0
    frames = 0
    try:
        while time.monotonic() - start < args.duration:
            if time.monotonic() - start >= next_query:
                # Idempotent across a restart, which must still fail verification.
                port.write(b"ticks on\nstatus\ndials\n")
                next_query += 12
            data = port.read(4096)
            output.extend(data)
            pending += data.decode("utf-8", errors="replace")
            while "\n" in pending:
                line, pending = pending.split("\n", 1)
                if line.startswith("Nixie frame at "):
                    frames += 1
                    if frames % 40 == 0:
                        print(line, flush=True)
                else:
                    print(line, flush=True)
    finally:
        port.write(b"ticks off\n")
        port.close()
        (args.output / "serial.log").write_bytes(output)
    report = summarize(output.decode("utf-8", errors="replace"), args.duration,
                       args.version, args.interval_ms, args.minimum_frames, args.max_jitter_ms)
    binary = ROOT / ".pio/build/esp32-devkitc-32e/firmware.bin"
    if binary.exists():
        # This is the locally built image; the firmware version is checked above.
        report["local_firmware_sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2), flush=True)
    if not report["pass"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
