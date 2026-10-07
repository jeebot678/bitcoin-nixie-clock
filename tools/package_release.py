#!/usr/bin/env python3
"""Package an already-built image as a signed GitHub release. No upload occurs."""
import argparse
import base64
import hashlib
import json
import pathlib
import re
import shutil
import subprocess

ROOT=pathlib.Path(__file__).resolve().parents[1]


def canonical(manifest):
    return ("\n".join(str(manifest[k]) for k in ("version","board","size","sha256","url"))+"\n").encode()


def package(binary,version,repository,key,output):
    if not re.fullmatch(r"(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})",version) or any(int(v)>65535 for v in version.split(".")):
        raise ValueError("version must be major.minor.patch, each component 0–65535")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9_.-]+",repository):
        raise ValueError("repository must be owner/repository")
    data=binary.read_bytes()
    if len(data)<1024 or len(data)>0x1E0000 or data[0]!=0xE9:
        raise ValueError("not an ESP32 firmware image that fits an OTA slot")
    # The compiled application embeds this version string through OtaConfig.h.
    # Require the caller to have built the matching version before signing.
    if (version+"\0").encode() not in data:
        raise ValueError("requested version is absent from the built image; rebuild with BTC_FIRMWARE_VERSION")
    manifest={"version":version,"board":"esp32-devkitc-32e-rev20","size":len(data),"sha256":hashlib.sha256(data).hexdigest(),"url":f"https://github.com/{repository}/releases/download/v{version}/firmware.bin"}
    payload=canonical(manifest)
    signature=subprocess.run(["openssl","dgst","-sha256","-sign",str(key)],input=payload,check=True,capture_output=True).stdout
    if len(signature)!=256:
        raise ValueError("signing key must be RSA-2048")
    # Verify below with files: OpenSSL cannot read both payload and signature
    # from a single standard input stream.
    output.mkdir(parents=True,exist_ok=True)
    (output/"canonical.txt").write_bytes(payload)
    (output/"signature.bin").write_bytes(signature)
    subprocess.run(["openssl","dgst","-sha256","-verify",str(ROOT/"data/cert/ota-public.pem"),"-signature",str(output/"signature.bin"),str(output/"canonical.txt")],check=True,capture_output=True)
    manifest["signature"]=base64.b64encode(signature).decode()
    shutil.copy2(binary,output/"firmware.bin")
    (output/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    return manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version",required=True)
    parser.add_argument("--repository",required=True)
    parser.add_argument("--binary",type=pathlib.Path,default=ROOT/".pio/build/esp32-devkitc-32e/firmware.bin")
    parser.add_argument("--key",type=pathlib.Path,default=ROOT/"secrets/ota-signing-key.pem")
    args=parser.parse_args()
    manifest=package(args.binary,args.version,args.repository,args.key,ROOT/"release"/("v"+args.version))
    print(f"Signed release v{manifest['version']} ready: firmware.bin + manifest.json ({manifest['size']} bytes). No files uploaded.")


if __name__=="__main__":
    main()
