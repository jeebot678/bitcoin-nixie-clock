#!/usr/bin/env python3
"""Collect LAN telemetry and trigger signed main-branch OTA checks without USB."""
import argparse
import datetime
import fcntl
import ipaddress
import json
import os
import pathlib
import re
import signal
import subprocess
import sys
import time
import tempfile
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "test/.build/telemetry"
MAX_BODY = 16384


class Clock:
    def __init__(self, hosts, port=8080, timeout=2):
        self.hosts = list(dict.fromkeys(hosts))
        if not self.hosts or any(not re.fullmatch(r"[A-Za-z0-9_.-]+", h) for h in self.hosts):
            raise ValueError("Use an IPv4 address or hostname, not a URL")
        self.port, self.timeout, self.last_host, self.device_id = port, timeout, None, None
        self.cached_ip = None
        # A local device request must not be sent through an HTTP proxy.
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def request(self, host, path, fields=None):
        body = urllib.parse.urlencode(fields).encode() if fields is not None else None
        request = urllib.request.Request(f"http://{host}:{self.port}{path}", data=body,
                                         headers={"Cache-Control": "no-cache"})
        with self.opener.open(request, timeout=self.timeout) as response:
            data = response.read(MAX_BODY + 1)
        if len(data) > MAX_BODY:
            raise ValueError("Device response exceeds the telemetry limit")
        value = json.loads(data)
        if not isinstance(value, dict):
            raise ValueError("Expected a telemetry object")
        return value

    def snapshot(self):
        failure = None
        # mDNS lookup can take seconds on macOS, outside the HTTP timeout.
        # Reuse the reported LAN address after discovery, checking identity on
        # every response; fall back to discovery when DHCP changes the address.
        hosts = ([self.cached_ip] if self.cached_ip else []) + ([self.last_host] if self.last_host else []) + self.hosts
        for host in dict.fromkeys(hosts):
            try:
                value = self.request(host, "/telemetry")
                validate(value)
                if self.device_id and value["device_id"]!=self.device_id:
                    raise ValueError("A different clock answered at this address")
                self.device_id=value["device_id"]
                self.last_host = host
                try:
                    address = ipaddress.IPv4Address(value.get("ip", ""))
                    if address.is_private and not address.is_unspecified:
                        self.cached_ip = str(address)
                except ipaddress.AddressValueError:
                    pass
                return value
            except (OSError, ValueError) as error:
                failure = error
        raise RuntimeError(f"Clock telemetry unavailable: {failure}")

    def command(self, action):
        self.snapshot()  # Resolve and validate this is the clock before POSTing.
        token = self.request(self.last_host, "/control-token").get("token", "")
        if not re.fullmatch(r"[0-9a-f]{32}", token):
            raise ValueError("Device control token is invalid")
        return self.request(self.last_host, "/control", {"token": token, "action": action})


def validate(value):
    if value.get("schema") != 1 or value.get("board") != "esp32-devkitc-32e-rev20":
        raise ValueError("This is not the expected clock telemetry")
    if not re.fullmatch(r"\d+\.\d+\.\d+", str(value.get("firmware", ""))):
        raise ValueError("Invalid firmware version")
    if not re.fullmatch(r"[0-9a-f]{12,16}", str(value.get("device_id", ""))) or not isinstance(value.get("dials"),dict):
        raise ValueError("Invalid device identity/dial data")
    for name in ("frequency", "range"):
        dial = value.get("dials", {}).get(name, {})
        if not isinstance(dial,dict) or not (isinstance(dial.get("mv"), int) and 0 <= dial["mv"] <= 4000
                and isinstance(dial.get("raw"), int) and 0 <= dial["raw"] <= 4095
                and dial.get("selected") in range(5) and dial.get("decoded") in range(-1, 5)):
            raise ValueError(f"Invalid {name} readings")
    for name in ("uptime_ms", "accepted_quotes", "nixie_frames", "refresh_ms", "snapshot_age_ms"):
        if not isinstance(value.get(name), int) or not 0 <= value[name] <= 0xFFFFFFFF:
            raise ValueError(f"Invalid {name}")
    if value["snapshot_age_ms"] > 5000:
        raise ValueError("Device snapshot is over five seconds old")


def atomic_json(path, value):
    with tempfile.NamedTemporaryFile(mode="w",prefix=path.name+".",suffix=".tmp",dir=path.parent,delete=False) as stream:
        temporary=pathlib.Path(stream.name)
        stream.write(json.dumps(value, indent=2) + "\n")
    try:os.replace(temporary,path)
    finally:temporary.unlink(missing_ok=True)


def record(output, snapshot, label=None):
    value = {"received_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
             "telemetry": snapshot}
    if label:
        value["label"] = label
    with (output/"write.lock").open("a") as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        atomic_json(output / "latest.json", value)
        path = output / "samples.jsonl"
        if path.exists() and path.stat().st_size > 10 * 1024 * 1024:
            os.replace(path, output / "samples.previous.jsonl")
        with path.open("a") as stream:
            stream.write(json.dumps(value, separators=(",", ":")) + "\n")
    return value


def collect(clock, output, interval, duration=0):
    output.mkdir(parents=True, exist_ok=True)
    with (output / "collector.lock").open("w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("A collector is already running in this directory.", flush=True)
            return
        atomic_json(output / "collector.pid.json", {"pid": os.getpid(), "script": str(pathlib.Path(__file__).resolve())})
        started = time.monotonic()
        previous = None
        while not duration or time.monotonic() - started < duration:
            tick = time.monotonic()
            try:
                sample = clock.snapshot()
                record(output, sample)
                connection = (sample["device_id"], sample["firmware"])
                if connection != previous:
                    print(f"Connected: device {connection[0]}, firmware {connection[1]}, host {clock.last_host}", flush=True)
                    previous = connection
                atomic_json(output / "connection.json", {"connected": True, "host": clock.last_host,
                            "firmware": sample["firmware"], "received_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()})
                delay = interval
            except (OSError, ValueError, RuntimeError) as error:
                atomic_json(output / "connection.json", {"connected": False, "error": str(error),
                            "checked_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()})
                if previous != "offline":
                    print(str(error), flush=True)
                    previous = "offline"
                delay = 3
            time.sleep(max(0, delay - (time.monotonic() - tick)))


def start(args):
    args.output.mkdir(parents=True, exist_ok=True)
    with (args.output / "collector.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("Collector is already running.")
            return
        fcntl.flock(lock, fcntl.LOCK_UN)
    command = [sys.executable, str(pathlib.Path(__file__).resolve()), "--host", args.host,
               "--port", str(args.port), "--output", str(args.output)]
    for host in args.fallback_host:
        command += ["--fallback-host", host]
    command += ["collect", "--interval", str(args.interval)]
    with (args.output / "collector.log").open("ab", buffering=0) as log:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                                   start_new_session=True, cwd=ROOT)
    print(f"Collector started (PID {process.pid}); output: {args.output}")


def wait_for_update(clock, expected, seconds):
    deadline = time.monotonic() + seconds
    next_check = time.monotonic() + 30
    while time.monotonic() < deadline:
        try:
            snapshot = clock.snapshot()
            if snapshot["firmware"] == expected and snapshot.get("boot_confirmed") is True:
                print(f"Verified running firmware {expected}")
                return
            # A check can see a cached old manifest or fail transiently. Keep
            # the explicit update request active, without interrupting HTTP
            # work, a queued check or a newly installed image's boot validation.
            if (snapshot["firmware"] != expected and snapshot.get("boot_confirmed") is True
                    and not snapshot.get("http_busy") and not snapshot.get("ota_queued")
                    and time.monotonic() >= next_check):
                next_check = time.monotonic() + 30
                print("Retrying signed update check…", flush=True)
                clock.command("ota")
        except (OSError, ValueError, RuntimeError):
            pass
        time.sleep(min(2, max(0, deadline - time.monotonic())))
    raise RuntimeError(f"Running firmware {expected} was not verified within {seconds:g} seconds")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="bitcoin-clock.local")
    parser.add_argument("--fallback-host", action="append", default=[])
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--output", type=pathlib.Path, default=DEFAULT_OUTPUT)
    sub = parser.add_subparsers(dest="operation", required=True)
    for operation in ("collect", "start"):
        command = sub.add_parser(operation)
        command.add_argument("--interval", type=float, default=0.5)
        if operation == "collect":
            command.add_argument("--duration", type=float, default=0)
    sub.add_parser("status")
    marker = sub.add_parser("mark");marker.add_argument("label")
    sub.add_parser("tube-test");sub.add_parser("readings");sub.add_parser("chart")
    ota = sub.add_parser("ota");ota.add_argument("--expect");ota.add_argument("--wait", type=float, default=0)
    args = parser.parse_args();args.output = args.output.resolve()
    if args.operation=="ota" and args.wait and not args.expect:parser.error("--wait requires --expect VERSION")
    clock = Clock([args.host] + args.fallback_host, args.port)
    if args.operation == "start":
        if args.interval < 0.25:parser.error("Use at least a 250 ms collection interval")
        start(args);return
    if args.operation == "collect":
        if args.interval < 0.25:parser.error("Use at least a 250 ms collection interval")
        collect(clock, args.output, args.interval, args.duration);return
    if args.operation in ("status", "mark"):
        snapshot = clock.snapshot();args.output.mkdir(parents=True, exist_ok=True)
        print(json.dumps(record(args.output, snapshot, getattr(args, "label", None)), indent=2));return
    action = "tubes" if args.operation == "tube-test" else args.operation
    try:
        print(json.dumps(clock.command(action)), flush=True)
    except (OSError, ValueError, RuntimeError):
        if args.operation != "ota" or not args.wait:
            raise
        # A restart/slow response can lose the acknowledgment after the clock
        # accepted the command. Verify its version before deciding to retry.
        print("Update acknowledgment unavailable; waiting for the device and retrying idle checks.", flush=True)
    if args.operation == "ota" and args.wait:
        wait_for_update(clock, args.expect, args.wait)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        raise SystemExit(str(error))
