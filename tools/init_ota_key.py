#!/usr/bin/env python3
"""Create the local OTA signing key once. Never upload the private key."""
import os
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
key = root / "secrets" / "ota-signing-key.pem"
public = root / "data" / "cert" / "ota-public.pem"
key.parent.mkdir(exist_ok=True, mode=0o700)
if not key.exists():
    subprocess.run(["openssl", "genrsa", "-out", str(key), "2048"], check=True, capture_output=True)
    os.chmod(key, 0o600)
subprocess.run(["openssl", "rsa", "-in", str(key), "-pubout", "-out", str(public)], check=True, capture_output=True)
print("OTA public key ready. Private signing key stays in ignored secrets/.")
