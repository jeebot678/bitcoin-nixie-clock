#pragma once
#include <atomic>

namespace btc {
// Keep new TLS connections from competing for ESP32 crypto hardware and
// transient handshake memory. Established stream reads do not take this gate.
inline std::atomic<bool>& tlsHandshakeGate() {
  static std::atomic<bool> held{false};
  return held;
}
class TlsLease {
 public:
  TlsLease() = default;
  TlsLease(const TlsLease&) = delete;
  TlsLease& operator=(const TlsLease&) = delete;
  ~TlsLease() { release(); }
  void release() { if (held_) {held_=false;tlsHandshakeGate().store(false);} }
  bool acquire() {
    if (held_) return true;
    bool expected=false;
    held_=tlsHandshakeGate().compare_exchange_strong(expected,true);
    return held_;
  }
  explicit operator bool() const { return held_; }
 private:
  bool held_=false;
};
}
