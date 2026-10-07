#pragma once
#include "Arduino.h"
constexpr int WL_CONNECTED=3,WIFI_STA=1,WIFI_AP_STA=3,WIFI_SCAN_FAILED=-2,WIFI_SCAN_RUNNING=-1;
class IPAddress{public:IPAddress(int a,int b,int c,int d):text(std::to_string(a)+"."+std::to_string(b)+"."+std::to_string(c)+"."+std::to_string(d)){}String toString()const{return text;}private:std::string text;};
struct WifiMock{
  int connection=0,currentMode=0,beginCalls=0,apStarts=0,apStops=0,scans=WIFI_SCAN_FAILED;bool ap=false;
  String lastSsid,lastPassword;std::vector<String> names;
  void persistent(bool){}void setAutoReconnect(bool){}void mode(int m){currentMode=m;}void setHostname(const char*){}
  int status()const{return connection;}void disconnect(bool,bool){connection=0;}
  void begin(const char*s,const char*p){lastSsid=s;lastPassword=p;++beginCalls;}
  bool softAP(const char*,const char*,int,int,int){ap=true;++apStarts;return true;}
  void softAPConfig(IPAddress,IPAddress,IPAddress){}IPAddress softAPIP()const{return IPAddress(192,168,4,1);}IPAddress localIP()const{return IPAddress(192,168,1,90);}
  void softAPdisconnect(bool){ap=false;++apStops;}void scanDelete(){scans=WIFI_SCAN_FAILED;}
  int scanComplete(){return scans;}void scanNetworks(bool){scans=WIFI_SCAN_RUNNING;}String SSID(int i){return names.at(i);}int RSSI(int){return -40;}
};extern WifiMock WiFi;
