#pragma once
#ifndef BTC_FIRMWARE_VERSION
#define BTC_FIRMWARE_VERSION "1.0.1"
#endif
namespace ota_config {
constexpr const char* kVersion=BTC_FIRMWARE_VERSION;
constexpr const char* kBoard="esp32-devkitc-32e-rev20";
// Public GitHub repo in owner/repository form. Empty disables remote checks.
constexpr const char* kRepository="jeebot678/bitcoin-nixie-clock";
constexpr const char* kBranch="main";
constexpr unsigned long kCheckIntervalMs=21600000; // Six hours + up to 15 min jitter.
}
