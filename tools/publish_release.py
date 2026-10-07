#!/usr/bin/env python3
"""Build, test, sign and publish firmware, then update the main rollout pointer.

Only run this when you intend to publish a device update. The private signing
key stays local; only the firmware and signed manifest go to GitHub.
"""
import argparse
import pathlib
import re
import shutil
import subprocess
import sys
from package_release import package

ROOT=pathlib.Path(__file__).resolve().parents[1]
REPOSITORY="jeebot678/bitcoin-nixie-clock"


def run(args):
    subprocess.run([str(a) for a in args],cwd=ROOT,check=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--version",required=True);args=parser.parse_args()
    if not re.fullmatch(r"(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})",args.version) or any(int(v)>65535 for v in args.version.split('.')):
        raise ValueError("Use major.minor.patch")
    branch=subprocess.check_output(["git","branch","--show-current"],cwd=ROOT,text=True).strip()
    if branch!="main":raise ValueError("Publish from main")
    if subprocess.check_output(["git","status","--porcelain"],cwd=ROOT,text=True).strip():raise ValueError("Commit your source changes before publishing")
    key=ROOT/"secrets/ota-signing-key.pem"
    if not key.exists():raise ValueError("Restore the original local signing key before publishing; do not generate a replacement")
    config=ROOT/"include/OtaConfig.h";text=config.read_text()
    current=re.search(r'#define BTC_FIRMWARE_VERSION "([^"]+)"',text).group(1)
    if tuple(map(int,args.version.split('.'))) <= tuple(map(int,current.split('.'))):raise ValueError("The new version must exceed the current version")
    config.write_text(text.replace(f'#define BTC_FIRMWARE_VERSION "{current}"',f'#define BTC_FIRMWARE_VERSION "{args.version}"'))
    pio=shutil.which("pio") or str(pathlib.Path.home()/".platformio/penv/bin/pio")
    run([pio,"run"]);run([sys.executable,"tools/run_tests.py"])
    output=ROOT/"release"/("v"+args.version)
    package(ROOT/".pio/build/esp32-devkitc-32e/firmware.bin",args.version,REPOSITORY,key,output)
    run(["git","add","include/OtaConfig.h"]);run(["git","commit","-m",f"Build firmware {args.version}"]);run(["git","push","origin","main"])
    notes=output/"release-notes.md";notes.write_text(f"Firmware {args.version} for the Rev20 ESP32 Bitcoin Nixie clock. Built and verified with the sanitized host test suites. Device rollout is controlled by ota/manifest.json on main.\n")
    run(["gh","release","create","v"+args.version,output/"firmware.bin",output/"manifest.json","--repo",REPOSITORY,"--target","main","--title","Firmware "+args.version,"--notes-file",notes])
    target=ROOT/"ota/manifest.json";target.parent.mkdir(exist_ok=True);shutil.copy2(output/"manifest.json",target)
    run(["git","add","ota/manifest.json"]);run(["git","commit","-m",f"Roll out signed firmware {args.version} from main"]);run(["git","push","origin","main"])
    print("Firmware release published and main rollout manifest updated.")


if __name__=="__main__":main()
