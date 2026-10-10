#pragma once
#include "Arduino.h"
#include <functional>
constexpr int WL_CONNECTED=3,WIFI_STA=1,WIFI_AP_STA=3,WIFI_SCAN_FAILED=-2,WIFI_SCAN_RUNNING=-1;
constexpr int WIFI_ALL_CHANNEL_SCAN=1,WIFI_CONNECT_AP_BY_SIGNAL=0,WIFI_AUTH_OPEN=0,WIFI_AUTH_WPA2_PSK=3,ARDUINO_EVENT_WIFI_STA_DISCONNECTED=5;
using WiFiEvent_t=int;struct WiFiEventInfo_t {struct {uint8_t reason;}wifi_sta_disconnected;};
class IPAddress{public:IPAddress(int a,int b,int c,int d):text(std::to_string(a)+"."+std::to_string(b)+"."+std::to_string(c)+"."+std::to_string(d)){}String toString()const{return text;}private:std::string text;};
struct WifiMock{
  int connection=0,currentMode=0,beginCalls=0,apStarts=0,apStops=0,scans=WIFI_SCAN_FAILED;bool ap=false,failAp=false;
  String lastSsid,lastPassword,apSsid,apPassword;std::vector<String> names;std::vector<int> strengths,securities,channels;
  int scanCalls=0,scanStops=0,scanMethod=0,sortMethod=1,minSecurity=WIFI_AUTH_WPA2_PSK;bool failScan=false;
  std::function<void(WiFiEvent_t,WiFiEventInfo_t)>disconnected;
  void persistent(bool){}void setAutoReconnect(bool){}void mode(int m){currentMode=m;}void setHostname(const char*){}
  int status()const{return connection;}void disconnect(bool,bool){connection=0;}
  void begin(const char*s,const char*p){lastSsid=s;lastPassword=p;++beginCalls;}
  void setScanMethod(int v){scanMethod=v;}void setSortMethod(int v){sortMethod=v;}void setMinSecurity(int v){minSecurity=v;}
  void onEvent(std::function<void(WiFiEvent_t,WiFiEventInfo_t)>callback,int){disconnected=callback;}
  bool softAP(const char*s,const char*p,int,int,int){apSsid=s;apPassword=p;ap=!failAp;++apStarts;return ap;}
  void softAPConfig(IPAddress,IPAddress,IPAddress){}IPAddress softAPIP()const{return IPAddress(192,168,4,1);}IPAddress localIP()const{return IPAddress(192,168,1,90);}
  void softAPdisconnect(bool){ap=false;++apStops;}void scanDelete(){scans=WIFI_SCAN_FAILED;}
  int scanComplete(){return scans;}int scanNetworks(bool,bool=false){++scanCalls;scans=failScan?WIFI_SCAN_FAILED:WIFI_SCAN_RUNNING;return scans;}
  String SSID(int i){return names.at(i);}int RSSI(int i){return strengths.empty()?-40:strengths.at(i);}int encryptionType(int i){return securities.empty()?WIFI_AUTH_WPA2_PSK:securities.at(i);}int channel(int i){return channels.empty()?1:channels.at(i);}
};extern WifiMock WiFi;
