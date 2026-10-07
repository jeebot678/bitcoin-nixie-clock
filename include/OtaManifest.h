#pragma once
#include <ArduinoJson.h>
#include <string>
#include <cstdio>
#include <cstring>
#include "OtaConfig.h"

namespace ota {
struct Version { unsigned value[3]={}; };
inline bool version(const char* text,Version& out) {
  if (!text) return false;
  for (int i=0;i<3;++i) {
    const char* start=text;
    if (*text<'0'||*text>'9') return false;
    unsigned value=0;
    while (*text>='0'&&*text<='9') { value=value*10+(*text++-'0'); if (value>65535) return false; }
    if (text-start>1&&*start=='0') return false;
    out.value[i]=value;
    if (i<2) { if (*text++!='.') return false; } else if (*text) return false;
  }
  return true;
}
inline bool newer(const char* candidate,const char* current) {
  Version a,b; if (!version(candidate,a)||!version(current,b)) return false;
  for (int i=0;i<3;++i) if (a.value[i]!=b.value[i]) return a.value[i]>b.value[i];
  return false;
}
struct Manifest { std::string version,board,sha256,url,signature; uint32_t size=0; };
inline std::string canonical(const Manifest& m) {
  return m.version+"\n"+m.board+"\n"+std::to_string(m.size)+"\n"+m.sha256+"\n"+m.url+"\n";
}
inline bool parseManifest(JsonVariantConst root,const char* repository,uint32_t slotSize,Manifest& m) {
  const char* v=root["version"],*b=root["board"],*h=root["sha256"],*u=root["url"],*s=root["signature"];
  Version parsed;
  if (!repository||!*repository||!v||!b||!h||!u||!s||!version(v,parsed)||strcmp(b,ota_config::kBoard)||strlen(h)!=64||strlen(u)>384||strlen(s)!=344||!root["size"].is<uint32_t>()) return false;
  uint32_t size=root["size"].as<uint32_t>();
  if (size<1024||size>slotSize) return false;
  for (size_t i=0;i<64;++i) if (!((h[i]>='0'&&h[i]<='9')||(h[i]>='a'&&h[i]<='f'))) return false;
  std::string expected=std::string("https://github.com/")+repository+"/releases/download/v"+v+"/firmware.bin";
  if (expected!=u) return false;
  m.version=v;m.board=b;m.sha256=h;m.url=u;m.signature=s;m.size=size;return true;
}
inline bool allowedDownload(const std::string& url) {
  const char* hosts[]={"https://github.com/","https://raw.githubusercontent.com/","https://release-assets.githubusercontent.com/","https://objects.githubusercontent.com/","https://github-releases.githubusercontent.com/"};
  for (const char* host:hosts) if (url.compare(0,strlen(host),host)==0) return url.find('\n')==std::string::npos&&url.find('\r')==std::string::npos;
  return false;
}
}
