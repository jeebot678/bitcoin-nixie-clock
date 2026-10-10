#!/usr/bin/env python3
"""Build and run sanitized host tests against the actual firmware sources."""
import base64
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

ROOT=pathlib.Path(__file__).resolve().parents[1]


def run(args,**kwargs):
    subprocess.run([str(a) for a in args],cwd=ROOT,check=True,**kwargs)


def main():
    build=ROOT/"test/.build";build.mkdir(exist_ok=True)
    compiler=os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("Install a C++11 compiler")
    dependency=ROOT/".pio/libdeps/esp32-devkitc-32e/ArduinoJson/src"
    if not dependency.is_dir():
        raise RuntimeError("Run pio run first to install the pinned ArduinoJson dependency")
    flags=[compiler,"-std=c++11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer","-Itest/host","-Iinclude","-Isrc","-I"+str(dependency)]
    report=json.loads((ROOT/"test/live_probe_report.json").read_text())
    epoch=int(datetime.datetime.fromisoformat(report["checked_at_utc"]).timestamp())
    for name,sources,args in [
        ("core_tests",["test/host/core_tests.cpp"],["test/live_fixtures",str(epoch)]),
        ("hardware_tests",["test/host/hardware_tests.cpp","src/Display.cpp","src/Provisioning.cpp"],[]),
        ("runtime_tests",["test/host/runtime_tests.cpp","src/Display.cpp","src/Provisioning.cpp"],[]),
        ("market_tests",["test/host/market_tests.cpp"],["test/live_fixtures",str(epoch)]),
    ]:
        run(flags+sources+["-o",build/name]);run([build/name]+args)
    # CI uses an ephemeral key; local testing uses the real signing key when
    # available. The host crypto adapters use OpenSSL for independent verification.
    key=ROOT/"secrets/ota-signing-key.pem";public=ROOT/"data/cert/ota-public.pem"
    if not key.exists():
        key=build/"test-key.pem";public=build/"test-public.pem"
        run(["openssl","genrsa","-out",key,"2048"],capture_output=True)
        run(["openssl","rsa","-in",key,"-pubout","-out",public],capture_output=True)
    pub=public.read_bytes()+b"\0"
    (build/"key_fixture.h").write_text('const uint8_t otaCaBundle[] asm("_binary_data_cert_x509_crt_bundle_bin_start") = {0};\nconst uint8_t otaPublicKey[] asm("_binary_data_cert_ota_public_pem_start") = {'+','.join(str(v) for v in pub)+'};\n')
    folder=build/"ota";folder.mkdir(exist_ok=True)
    binary=b"\xe9"+bytes(range(256))*20
    current=re.search(r'#define BTC_FIRMWARE_VERSION "([^"]+)"',(ROOT/"include/OtaConfig.h").read_text()).group(1)
    parts=list(map(int,current.split('.')))
    for i in (2,1,0):
        if parts[i]<65535:
            parts[i]+=1
            parts[i+1:]=[0]*(2-i)
            break
    else:
        raise ValueError("Firmware version has exhausted its range")
    candidate='.'.join(map(str,parts))
    for version,name in ((candidate,"manifest.json"),(current,"current-manifest.json")):
        manifest={"version":version,"board":"esp32-devkitc-32e-rev20","size":len(binary),"sha256":hashlib.sha256(binary).hexdigest(),"url":f"https://github.com/jeebot678/bitcoin-nixie-clock/releases/download/v{version}/firmware.bin"}
        canonical=("\n".join(str(manifest[k]) for k in ("version","board","size","sha256","url"))+"\n").encode()
        manifest["signature"]=base64.b64encode(subprocess.run(["openssl","dgst","-sha256","-sign",str(key)],input=canonical,check=True,capture_output=True).stdout).decode()
        (folder/name).write_text(json.dumps(manifest))
    (folder/"firmware.bin").write_bytes(binary)
    openssl_flags=[]
    if sys.platform=="darwin":
        prefix=pathlib.Path(subprocess.check_output(["brew","--prefix","openssl@3"],text=True).strip())
        openssl_flags=["-I"+str(prefix/"include"),"-L"+str(prefix/"lib")]
    run(flags+["-Itest/.build"]+openssl_flags+["test/host/ota_tests.cpp","-lcrypto","-o",build/"ota_tests"])
    run([build/"ota_tests",folder])
    run(flags+["-Itest/host/tls"]+openssl_flags+["test/host/tls_tests.cpp","-lcrypto","-o",build/"tls_tests"])
    run([build/"tls_tests"])
    if shutil.which("node"):
        text=(ROOT/"src/Provisioning.cpp").read_text();script=text.split("<script>",1)[1].split("</script>",1)[0]
        (build/"portal.js").write_text(script);run(["node","--check",build/"portal.js"])
        run(["node","test/host/portal_tests.cjs",build/"portal.js"])
    print("PASS: all sanitized host suites and portal JavaScript behavior")


if __name__=="__main__":
    main()
