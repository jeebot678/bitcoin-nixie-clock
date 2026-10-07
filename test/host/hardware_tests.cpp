#include <cassert>
#include <iostream>
#include "Arduino.h"
#include "SPI.h"
#include "Provisioning.h"
#include "Display.h"
#include "WifiCredentials.h"

SerialMock Serial;EspMock ESP;WifiMock WiFi;uint32_t testMillis=100;
std::map<int,int>pins,modes,adc;std::vector<GpioEvent>gpioEvents;std::vector<SpiPacket>packets;
std::string pageToken(WebServer&web){web.request("/",HTTP_GET);assert(web.statusCode==200);std::string marker="name=\"token\" value=\"";size_t start=web.body.value.find(marker);assert(start!=std::string::npos);return web.body.value.substr(start+marker.size(),32);}
void provisioningTests(){
  Provisioning p;p.begin();WebServer&web=*servers().back();assert(p.portalActive()&&web.running&&WiFi.ap);
  std::string token=pageToken(web);assert(token.size()==32);
  web.request("/connect",HTTP_POST,{{"ssid","home"},{"password","password123"},{"token","bad"}});assert(web.statusCode==403&&!WiFi.beginCalls);
  web.request("/connect",HTTP_POST,{{"ssid","home"},{"password","short"},{"token",token}});assert(web.statusCode==400);
  web.request("/connect",HTTP_POST,{{"ssid","home"},{"password","password123"},{"token",token}},{{"Origin","https://evil.com"}});assert(web.statusCode==403);
  web.request("/connect",HTTP_POST,{{"ssid","home"},{"password","wrongpass"},{"token",token}});assert(web.statusCode==202&&WiFi.lastSsid=="home");
  testMillis+=25001;p.loop(testMillis);assert(p.portalActive()&&prefMock().blobs["btc-wifi:credentials"].empty());web.request("/status",HTTP_GET);assert(web.body.value.find("Could not connect")!=std::string::npos);
  web.request("/connect",HTTP_POST,{{"ssid","home"},{"password","password123"},{"token",token}});assert(web.statusCode==202);
  WiFi.connection=WL_CONNECTED;p.loop(testMillis);assert(prefMock().blobs["btc-wifi:credentials"].size()==sizeof(btc::WifiCredentials));assert(p.portalActive());
  web.request("/status",HTTP_GET);assert(web.body.value.find("\"connected\":true")!=std::string::npos);
  testMillis+=8001;p.loop(testMillis);assert(!p.portalActive()&&!web.running&&!WiFi.ap&&WiFi.currentMode==WIFI_STA);
  WiFi.connection=0;int beginCalls=WiFi.beginCalls;p.loop(testMillis);assert(WiFi.beginCalls>beginCalls);
  testMillis+=30000;p.loop(testMillis);beginCalls=WiFi.beginCalls;testMillis+=30001;p.loop(testMillis);assert(WiFi.beginCalls>beginCalls);
  testMillis+=60001;p.loop(testMillis);assert(p.portalActive());
  p.openSetup();token=pageToken(web);web.request("/connect",HTTP_POST,{{"ssid","new-home"},{"password","password456"},{"token",token}});prefMock().failWrite=true;WiFi.connection=WL_CONNECTED;p.loop(testMillis);assert(!p.online()&&p.portalActive());
  btc::WifiCredentials old;memcpy(&old,prefMock().blobs["btc-wifi:credentials"].data(),sizeof(old));assert(strcmp(old.ssid,"home")==0);prefMock().failWrite=false;
  WiFi.names={String("<script>alert(1)</script>"),String("home")};WiFi.scans=2;web.request("/networks",HTTP_GET);assert(web.statusCode==200);
  p.forget();assert(prefMock().blobs["btc-wifi:credentials"].empty()&&p.portalActive());
  // Reboot with a known-good atomic record skips AP startup entirely.
  strcpy(old.ssid,"known");strcpy(old.password,"password123");Preferences prefs;prefs.begin("btc-wifi",false);prefs.putBytes("credentials",&old,sizeof(old));WiFi={};
  Provisioning rebooted;rebooted.begin();assert(!rebooted.portalActive()&&WiFi.lastSsid=="known");
  testMillis+=25001;rebooted.loop(testMillis);assert(rebooted.portalActive());
}
void displayTests(){
  for(int cs:{16,17,21,22,25,26})pins[cs]=LOW;
  display::begin();for(int cs:{16,17,21,22,25,26}){assert(pins[cs]==HIGH);auto first=std::find_if(gpioEvents.begin(),gpioEvents.end(),[&](const GpioEvent&e){return e.pin==cs;});assert(first!=gpioEvents.end()&&!first->mode&&first->value==HIGH);}
  for(const auto&p:packets)if(p.bus==VSPI){assert(p.bytes.size()==16&&p.bytes[0]==0xAA);for(int i=1;i<16;++i)assert(p.bytes[i]==0x80);}
  packets.clear();display::price(123456.49);assert(packets.size()==6);int chip[6]={16,17,21,22,25,26};for(int i=0;i<6;++i){auto&p=packets[i];assert(p.selected==chip[i]&&p.bytes[0]==0xAA);for(int b=1;b<16;++b)assert(p.bytes[b]==(b==i+1?0xC0:0x80));}
  packets.clear();display::price(123456.49);assert(packets.empty());display::price(123457);assert(packets.size()==1&&packets[0].selected==26&&packets[0].bytes[7]==0xC0);
  packets.clear();display::price(1000000);assert(packets.size()==6);packets.clear();display::price(1000000);assert(packets.empty());
  display::price(0.49);assert(packets.size()==6);for(const auto&p:packets)assert(p.bytes[10]==0xC0);
  packets.clear();btc::Plot plot={};plot.valid=1;plot.rows[0]=0;display::plot(plot);assert(packets.size()==8);int order[6]={3,4,5,2,1,0};
  for(int digit=0;digit<8;++digit){const auto&p=packets[digit];assert(p.bus==HSPI&&p.bytes.size()==12);for(int slot=0;slot<6;++slot){assert(p.bytes[slot*2]==digit+1);assert(p.bytes[slot*2+1]==(digit==0&&order[slot]==0?0x08:0));}}
  packets.clear();display::plot(plot);assert(packets.empty());
}
int main(){displayTests();provisioningTests();std::cout<<"PASS: real display transport and provisioning code with host hardware/network models\n";}
