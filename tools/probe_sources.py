#!/usr/bin/env python3
"""Low-volume, TLS-verified public endpoint checks; never load-test providers."""
import argparse
import concurrent.futures
import datetime
import http.client
import json
import pathlib
import ssl
import time
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parents[1]
def public_headers(headers):
    return {k:v for k,v in headers.items() if k.lower() in {"date","age","cache-control","content-type","content-length","content-encoding","transfer-encoding","retry-after"} or "ratelimit" in k.lower() or "rate-limit" in k.lower() or "used-weight" in k.lower()}

ENDPOINTS = {
    "coinbase": "https://api.exchange.coinbase.com/products/BTC-USD/ticker",
    "kraken": "https://api.kraken.com/0/public/Ticker?pair=XBTUSD,USDTUSD",
    "bitstamp": "https://www.bitstamp.net/api/v2/ticker/btcusd/",
    "gemini": "https://api.gemini.com/v2/ticker/btcusd",
    "bitfinex": "https://api-pub.bitfinex.com/v2/ticker/tBTCUSD",
    "binance": "https://data-api.binance.vision/api/v3/ticker/price?symbol=BTCUSDT",
    "okx": "https://www.okx.com/api/v5/market/ticker?instId=BTC-USDT",
    "kucoin": "https://api.kucoin.com/api/v1/market/orderbook/level1?symbol=BTC-USDT",
    "gate": "https://api.gateio.ws/api/v4/spot/tickers?currency_pair=BTC_USDT",
    "bitget": "https://api.bitget.com/api/v2/spot/market/tickers?symbol=BTCUSDT",
    "mexc": "https://api.mexc.com/api/v3/ticker/price?symbol=BTCUSDT",
    "bybit": "https://api.bybit.com/v5/market/tickers?category=spot&symbol=BTCUSDT",
}


def fetch(name, url, directory):
    start = time.monotonic()
    result = {"name": name, "url": url}
    try:
        # Match the ESP32's HTTP/1.0, uncompressed, no-redirect requests and
        # validate against the same Mozilla roots embedded in the firmware.
        target = urllib.parse.urlsplit(url)
        if target.scheme != "https":
            raise ValueError("HTTPS required")
        connection = http.client.HTTPSConnection(target.hostname, timeout=15, context=ssl.create_default_context(cafile=str(ROOT / "data/cert/mozilla.pem")))
        connection._http_vsn = 10
        connection._http_vsn_str = "HTTP/1.0"
        try:
            connection.request("GET", target.path + ("?" + target.query if target.query else ""), headers={"Host": target.netloc, "User-Agent": "BitcoinClock/1.0", "Accept": "application/json", "Accept-Encoding": "identity", "Connection": "close", "Cache-Control": "no-cache"})
            response = connection.getresponse()
            body = response.read(160001)
            headers = dict(response.getheaders())
            result.update(status=response.status, headers=public_headers(headers), bytes=len(body))
            if response.status != 200:
                raise ValueError("HTTP " + str(response.status))
            if response.getheader("Transfer-Encoding") or response.getheader("Content-Encoding", "identity") != "identity":
                raise ValueError("unexpected transfer/content encoding")
        finally:
            connection.close()
        if len(body) > 160000:
            raise ValueError("oversized response")
        payload = json.loads(body)
        directory.mkdir(parents=True, exist_ok=True)
        (directory / (name + ".json")).write_text(json.dumps(payload))
        result["json_valid"] = True
    except Exception as error:
        result["error"] = str(error)
    result["elapsed_ms"] = round((time.monotonic() - start) * 1000)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", action="store_true")
    parser.add_argument("--limits-only", action="store_true")
    args = parser.parse_args()
    if args.limits_only:
        result = fetch("binance_limits", "https://data-api.binance.vision/api/v3/exchangeInfo?symbol=BTCUSDT", ROOT / "test" / "live_fixtures")
        report_path = ROOT / "test" / "live_probe_report.json"
        report = json.loads(report_path.read_text())
        report["binance_limits_check"] = result
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        if result.get("json_valid"):
            print(json.loads((ROOT / "test/live_fixtures/binance_limits.json").read_text()).get("rateLimits"))
            return 0
        print(result)
        return 1
    endpoints = dict(ENDPOINTS)
    if args.history:
        now = int(time.time())
        for name, window, minutes in [("5m", 300, 1), ("30m", 1800, 1), ("hour", 3600, 1), ("day", 86400, 5), ("week", 604800, 60)]:
            step=minutes*60
            first=(now-window+step-1)//step*step
            start=first-step
            endpoints["kraken_" + name] = f"https://api.kraken.com/0/public/OHLC?pair=XBTUSD&interval={minutes}&since={start-step}"
            count = (now//step*step-start)//step
            endpoints["bitstamp_" + name] = f"https://www.bitstamp.net/api/v2/ohlc/btcusd/?step={step}&limit={count}&end={now//step*step-1}&exclude_current_candle=true"
        for rows in (1,48):
            last=now//3600*3600;start=last-rows*3600
            endpoints[f"kraken_week_delta_{rows}"]=f"https://api.kraken.com/0/public/OHLC?pair=XBTUSD&interval=60&since={start-3600}"
            endpoints[f"bitstamp_week_delta_{rows}"]=f"https://www.bitstamp.net/api/v2/ohlc/btcusd/?step=3600&limit={rows}&end={last-1}&exclude_current_candle=true"
    directory = ROOT / "test" / "live_fixtures"
    groups = {}
    for name, url in endpoints.items():
        groups.setdefault(name.split("_", 1)[0], []).append((name, url))
    def probe_group(group):
        found = []
        for index, item in enumerate(group):
            if index:
                time.sleep(5)  # Shared quote/history allowance, as on device.
            found.append(fetch(*item, directory))
        return found
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = [result for group in pool.map(probe_group, groups.values()) for result in group]
    report = {"checked_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "transport": "HTTPS with embedded Mozilla roots; HTTP/1.0; identity encoding; no redirects", "results": results, **({"history_epoch": now} if args.history else {})}
    previous = ROOT / "test/live_probe_report.json"
    if previous.exists():
        prior = json.loads(previous.read_text())
        if "binance_limits_check" in prior:
            report["binance_limits_check"] = prior["binance_limits_check"]
    (ROOT / "test" / "live_probe_report.json").write_text(json.dumps(report, indent=2) + "\n")
    for result in results:
        print(f"{result['name']:18} HTTP {result.get('status', '-'):3} {result['elapsed_ms']:5} ms {result.get('error', 'JSON OK')}")
    return 0 if all(r.get("json_valid") for r in results if r["name"] != "bybit") else 1


if __name__ == "__main__":
    raise SystemExit(main())
