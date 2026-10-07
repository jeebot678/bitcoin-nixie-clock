#!/usr/bin/env python3
"""Build Arduino-ESP32's sorted subject/SPKI root bundle using only stdlib.

The input is Mozilla's CA extract from https://curl.se/docs/caextract.html.
Certificates are public trust roots, not secrets. DER extraction follows the
X.509 TBSCertificate structure; no cryptography or pip dependency is needed.
"""
import argparse
import base64
import hashlib
import pathlib
import re
import ssl
import struct
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]


def tlv(data, offset):
    start = offset
    tag = data[offset]
    length = data[offset + 1]
    offset += 2
    if length & 128:
        n = length & 127
        length = int.from_bytes(data[offset:offset + n], "big")
        offset += n
    end = offset + length
    if end > len(data):
        raise ValueError("truncated DER")
    return tag, start, offset, end


def root_parts(der):
    _, _, outer, _ = tlv(der, 0)
    _, _, cursor, _ = tlv(der, outer)
    if der[cursor] == 0xA0:
        cursor = tlv(der, cursor)[3]
    for _ in range(4):  # serial, signature, issuer, validity
        cursor = tlv(der, cursor)[3]
    _, start, _, cursor = tlv(der, cursor)
    subject = der[start:cursor]
    _, start, _, end = tlv(der, cursor)
    return subject, der[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    directory = ROOT / "data" / "cert"
    directory.mkdir(parents=True, exist_ok=True)
    pem = directory / "mozilla.pem"
    if args.download:
        with urllib.request.urlopen("https://curl.se/ca/cacert.pem", context=ssl.create_default_context(), timeout=20) as response:
            pem.write_bytes(response.read(400000))
    roots = sorted(set(root_parts(base64.b64decode(block)) for block in re.findall(rb"-----BEGIN CERTIFICATE-----\s*(.*?)-----END CERTIFICATE-----", pem.read_bytes(), re.S)))
    if len(roots) < 100:
        raise ValueError("incomplete trust store")
    bundle = struct.pack(">H", len(roots)) + b"".join(struct.pack(">HH", len(subject), len(key)) + subject + key for subject, key in roots)
    (directory / "x509_crt_bundle.bin").write_bytes(bundle)
    print(f"{len(roots)} CA roots; {len(bundle)} bytes; PEM SHA256 {hashlib.sha256(pem.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
