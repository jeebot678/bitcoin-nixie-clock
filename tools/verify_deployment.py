#!/usr/bin/env python3
"""Verify the public main rollout manifest and its release image over HTTPS."""
import base64
import hashlib
import http.client
import json
import pathlib
import ssl
import subprocess
import urllib.parse
from package_release import canonical

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPOSITORY = "jeebot678/bitcoin-nixie-clock"
MANIFEST_URL = f"https://raw.githubusercontent.com/{REPOSITORY}/main/ota/manifest.json"
HOSTS = {"github.com", "raw.githubusercontent.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com", "github-releases.githubusercontent.com"}


def download(url, maximum):
    for _ in range(5):
        target = urllib.parse.urlsplit(url)
        if target.scheme != "https" or target.hostname not in HOSTS or target.username or target.password or target.port not in (None, 443):
            raise ValueError("Unexpected update redirect")
        connection = http.client.HTTPSConnection(target.hostname, timeout=15, context=ssl.create_default_context(cafile=str(ROOT / "data/cert/mozilla.pem")))
        connection._http_vsn = 10
        connection._http_vsn_str = "HTTP/1.0"
        try:
            connection.request("GET", target.path + ("?" + target.query if target.query else ""), headers={"Host": target.netloc, "User-Agent": "BitcoinClock-OTA/1.0", "Accept-Encoding": "identity", "Connection": "close"})
            response = connection.getresponse()
            if response.status in (301, 302, 303, 307, 308):
                url = response.getheader("Location", "")
                continue
            if response.status != 200:
                raise ValueError(f"Update HTTP {response.status}")
            length = int(response.getheader("Content-Length", "-1"))
            if not 0 < length <= maximum or response.getheader("Transfer-Encoding") or response.getheader("Content-Encoding", "identity") != "identity":
                raise ValueError("Update response headers incompatible with firmware")
            body = response.read(maximum + 1)
            if len(body) != length:
                raise ValueError("Truncated or oversized update")
            return body
        finally:
            connection.close()
    raise ValueError("Too many update redirects")


def main():
    manifest = json.loads(download(MANIFEST_URL, 4096))
    expected = f"https://github.com/{REPOSITORY}/releases/download/v{manifest['version']}/firmware.bin"
    if manifest["board"] != "esp32-devkitc-32e-rev20" or manifest["url"] != expected:
        raise ValueError("Wrong board/release URL")
    folder = ROOT / "test/.build/deployment"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "canonical.txt").write_bytes(canonical(manifest))
    (folder / "signature.bin").write_bytes(base64.b64decode(manifest["signature"], validate=True))
    subprocess.run(["openssl", "dgst", "-sha256", "-verify", str(ROOT / "data/cert/ota-public.pem"), "-signature", str(folder / "signature.bin"), str(folder / "canonical.txt")], check=True, capture_output=True)
    image = download(manifest["url"], 0x1E0000)
    if len(image) != manifest["size"] or hashlib.sha256(image).hexdigest() != manifest["sha256"] or image[0] != 0xE9 or (manifest["version"] + "\0").encode() not in image:
        raise ValueError("Downloaded image differs from signed manifest")
    local = ROOT / "ota/manifest.json"
    if local.exists() and json.loads(local.read_text()) != manifest:
        raise ValueError("Public main manifest differs from checkout")
    print(f"PASS: main manifest signature, HTTPS redirects/headers, image size/SHA-256 and embedded firmware version {manifest['version']} ({len(image)} bytes)")


if __name__ == "__main__":
    main()
