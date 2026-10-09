#pragma once
#include <stdint.h>

namespace btc {
enum class WifiSetupState : uint8_t {
  Waiting, Connecting, Connected, ConnectionFailed, SaveFailed, ApFailed
};
inline const char* setupStateName(WifiSetupState state) {
  switch (state) {
    case WifiSetupState::Waiting: return "connect wifi";
    case WifiSetupState::Connecting: return "connecting";
    case WifiSetupState::Connected: return "connected";
    case WifiSetupState::ConnectionFailed: return "wifi failed";
    case WifiSetupState::SaveFailed: return "save failed";
    case WifiSetupState::ApFailed: return "ap error";
  }
  return "unknown";
}
}
