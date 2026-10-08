#pragma once
#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>

class Provisioning {
 public:
  void begin();
  void loop(uint32_t now);
  void openSetup();
  void forget();
  bool online() const;
  bool portalActive() const { return portal_; }
 private:
  WebServer web_{80}; DNSServer dns_; Preferences prefs_;
  String savedSsid_,savedPassword_,pendingSsid_,pendingPassword_,token_,message_;
  bool portal_=false,manual_=false,trying_=false,pending_=false,wasOnline_=false;
  uint32_t attemptAt_=0,disconnectedAt_=0,retryAt_=0,closeAt_=0;
  void startPortal();
  void stopPortal();
  void connect(const String& ssid,const String& password,bool pending);
  void root();
  void credentials();
  void status();
  void networks();
};
