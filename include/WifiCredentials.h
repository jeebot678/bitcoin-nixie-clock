#pragma once
#include "ClockCore.h"
namespace btc {
struct WifiCredentials { uint32_t version = 1; char ssid[33] = {}; char password[65] = {}; };
inline bool validStoredCredentials(const WifiCredentials& record) {
  return record.version==1 && memchr(record.ssid,0,sizeof(record.ssid)) && memchr(record.password,0,sizeof(record.password)) && validCredentials(record.ssid,record.password);
}
}
