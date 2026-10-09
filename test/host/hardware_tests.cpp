#include <cassert>
#include <array>
#include <iostream>
#include "Arduino.h"
#include "SPI.h"
#include "Provisioning.h"
#include "Display.h"
#include "WifiCredentials.h"
#include "LedMatrixMap.h"
#include "MatrixText.h"

SerialMock Serial;EspMock ESP;WifiMock WiFi;uint32_t testMillis=100;
std::map<int,int>pins,modes,adc;std::vector<GpioEvent>gpioEvents;std::vector<SpiPacket>packets;
std::string pageToken(WebServer&web){web.request("/",HTTP_GET);assert(web.statusCode==200);std::string marker="name=\"token\" value=\"";size_t start=web.body.value.find(marker);assert(start!=std::string::npos);return web.body.value.substr(start+marker.size(),32);}
void provisioningTests(){
  Provisioning p;p.begin();WebServer&web=*servers().back();assert(p.portalActive()&&web.running&&WiFi.ap);
  assert(p.setupState()==btc::WifiSetupState::Waiting);
  assert(WiFi.apSsid.startsWith("BitcoinClock-")&&WiFi.apPassword.isEmpty());
  assert(!prefMock().strings.count("btc-wifi:setup-key")&&Serial.log.find("(no password)")!=std::string::npos);
  std::string token=pageToken(web);assert(token.size()==32);
  web.request("/connect",HTTP_POST,{{"ssid","home"},{"password","password123"},{"token","bad"}});assert(web.statusCode==403&&!WiFi.beginCalls);
  web.request("/connect",HTTP_POST,{{"ssid","home"},{"password","short"},{"token",token}});assert(web.statusCode==400);
  web.request("/connect",HTTP_POST,{{"ssid","home"},{"password","password123"},{"token",token}},{{"Origin","https://evil.com"}});assert(web.statusCode==403);
  web.request("/connect",HTTP_POST,{{"ssid","home"},{"password","wrongpass"},{"token",token}});assert(web.statusCode==202&&WiFi.lastSsid=="home");
  assert(p.setupState()==btc::WifiSetupState::Connecting);
  testMillis+=25001;p.loop(testMillis);assert(p.portalActive()&&prefMock().blobs["btc-wifi:credentials"].empty());web.request("/status",HTTP_GET);assert(web.body.value.find("Could not connect")!=std::string::npos);
  assert(p.setupState()==btc::WifiSetupState::ConnectionFailed);
  web.request("/connect",HTTP_POST,{{"ssid","home"},{"password","password123"},{"token",token}});assert(web.statusCode==202);
  WiFi.connection=WL_CONNECTED;p.loop(testMillis);assert(prefMock().blobs["btc-wifi:credentials"].size()==sizeof(btc::WifiCredentials));assert(p.portalActive());
  assert(p.setupState()==btc::WifiSetupState::Connected);
  web.request("/status",HTTP_GET);assert(web.body.value.find("\"connected\":true")!=std::string::npos);
  testMillis+=8001;p.loop(testMillis);assert(!p.portalActive()&&!web.running&&!WiFi.ap&&WiFi.currentMode==WIFI_STA);
  WiFi.connection=0;int beginCalls=WiFi.beginCalls;p.loop(testMillis);assert(WiFi.beginCalls>beginCalls);
  assert(p.setupState()==btc::WifiSetupState::Connecting);
  testMillis+=30000;p.loop(testMillis);beginCalls=WiFi.beginCalls;testMillis+=30001;p.loop(testMillis);assert(WiFi.beginCalls>beginCalls);
  testMillis+=60001;p.loop(testMillis);assert(p.portalActive()&&WiFi.apPassword.isEmpty());
  p.openSetup();token=pageToken(web);web.request("/connect",HTTP_POST,{{"ssid","new-home"},{"password","password456"},{"token",token}});prefMock().failWrite=true;WiFi.connection=WL_CONNECTED;p.loop(testMillis);assert(!p.online()&&p.portalActive());
  assert(p.setupState()==btc::WifiSetupState::SaveFailed);
  btc::WifiCredentials old;memcpy(&old,prefMock().blobs["btc-wifi:credentials"].data(),sizeof(old));assert(strcmp(old.ssid,"home")==0);prefMock().failWrite=false;
  WiFi.names={String("<script>alert(1)</script>"),String("home")};WiFi.scans=2;web.request("/networks",HTTP_GET);assert(web.statusCode==200);
  p.forget();assert(prefMock().blobs["btc-wifi:credentials"].empty()&&p.portalActive());
  assert(p.setupState()==btc::WifiSetupState::Waiting);
  // Reboot with a known-good atomic record skips AP startup entirely.
  strcpy(old.ssid,"known");strcpy(old.password,"password123");Preferences prefs;prefs.begin("btc-wifi",false);prefs.putBytes("credentials",&old,sizeof(old));prefs.putString("setup-key","btc-legacy-key");WiFi={};
  Provisioning rebooted;rebooted.begin();assert(!rebooted.portalActive()&&WiFi.lastSsid=="known");
  assert(rebooted.setupState()==btc::WifiSetupState::Connecting);
  assert(!prefMock().strings.count("btc-wifi:setup-key"));
  testMillis+=25001;rebooted.loop(testMillis);assert(rebooted.portalActive()&&WiFi.apPassword.isEmpty());
  assert(rebooted.setupState()==btc::WifiSetupState::ConnectionFailed);
  prefs.remove("credentials");WiFi={};WiFi.failAp=true;
  Provisioning failedAp;failedAp.begin();assert(!failedAp.portalActive()&&failedAp.setupState()==btc::WifiSetupState::ApFailed);
  WiFi.failAp=false;failedAp.openSetup();assert(failedAp.portalActive()&&failedAp.setupState()==btc::WifiSetupState::Waiting);
}
using Matrix=std::array<std::array<bool,21>,13>;
Matrix matrixPackets(){
  uint8_t registers[6][8]={};unsigned count=0;
  for(const auto&p:packets)if(p.bus==HSPI){++count;assert(p.bytes.size()==12);for(int slot=0;slot<6;++slot){uint8_t reg=p.bytes[slot*2];assert(reg>=1&&reg<=8);registers[config::kMaxShiftOrder[slot]][reg-1]=p.bytes[slot*2+1];}}
  assert(count==8);Matrix image={};
  for(uint8_t row=0;row<13;++row)for(uint8_t col=0;col<21;++col){auto a=led_matrix::mapPixel(row,col);image[row][col]=(registers[a.driver][a.digit]&(1U<<a.segmentBit))!=0;}
  return image;
}
bool blankPacket(const SpiPacket&p){for(unsigned i=1;i<p.bytes.size();++i)if(p.bytes[i]!=0x80)return false;return true;}
void setupDisplayTests(){
  packets.clear();display::setupStatus(btc::WifiSetupState::Waiting,1000);Matrix prompt=matrixPackets();
  for(int row:{0,6,12})for(bool lit:prompt[row])assert(!lit);
  // The physical SPI frame spells C at the upper left and centered W below.
  uint8_t c[5]={3,4,4,4,3},w[5]={5,5,7,7,5};
  for(int row=0;row<5;++row)for(int col=0;col<3;++col){assert(prompt[row+1][col]==bool(c[row]&(1U<<(2-col))));assert(prompt[row+7][col+3]==bool(w[row]&(1U<<(2-col))));}
  packets.clear();display::setupStatus(btc::WifiSetupState::Waiting,1025);assert(packets.empty());
  display::setupStatus(btc::WifiSetupState::Waiting,1720);Matrix moved=matrixPackets();assert(moved!=prompt);
  for(int row=7;row<12;++row)assert(moved[row]==prompt[row]);
  packets.clear();display::setupStatus(btc::WifiSetupState::Connecting,1800);Matrix connecting=matrixPackets();
  for(int row:{0,1,2,3,9,10,11,12})for(bool lit:connecting[row])assert(!lit);
  for(int row=0;row<5;++row)for(int col=0;col<3;++col)assert(connecting[row+4][col]==bool(c[row]&(1U<<(2-col))));
  packets.clear();display::setupStatus(btc::WifiSetupState::Connecting,4000);assert(matrixPackets()!=connecting);
  for(auto state:{btc::WifiSetupState::Connected,btc::WifiSetupState::ConnectionFailed,btc::WifiSetupState::SaveFailed,btc::WifiSetupState::ApFailed}){
    packets.clear();display::setupStatus(state,5000);Matrix image=matrixPackets();bool any=false;for(auto row:image)for(bool lit:row)any|=lit;assert(any);
  }
  assert(matrix_text::width("CONNECT")==27&&matrix_text::width("WIFI")==15);
  assert(matrix_text::offset(27,0)==0&&matrix_text::offset(27,1320)==-6&&matrix_text::offset(27,2640)==0);
  Matrix upper={};matrix_text::drawLine("Connect wifi",1,1234,[&](uint8_t r,uint8_t c){assert(r<13&&c<21);upper[r][c]=true;});
  Matrix lower={};matrix_text::drawLine("CONNECT WIFI",1,1234,[&](uint8_t r,uint8_t c){assert(r<13&&c<21);lower[r][c]=true;});assert(upper==lower);
  // Scroll timing also survives millis rollover, and leaving text restores the chart.
  packets.clear();display::setupStatus(btc::WifiSetupState::Waiting,UINT32_MAX-300);Matrix start=matrixPackets();
  packets.clear();display::setupStatus(btc::WifiSetupState::Waiting,419);assert(matrixPackets()!=start);
  packets.clear();btc::Plot chart={};chart.valid=1;chart.rows[0]=0;display::plot(chart);Matrix plotted=matrixPackets();unsigned lit=0;for(auto row:plotted)for(bool pixel:row)lit+=pixel;assert(lit==1&&plotted[0][0]);
  // Begin from six lit tubes: all five others are blanked before the chase.
  display::price(0.49);packets.clear();display::setupZero(1000);assert(packets.size()==5);for(const auto&p:packets)assert(blankPacket(p)&&p.selected!=16);
  int chip[6]={16,17,21,22,25,26};
  for(int step=1;step<=6;++step){
    packets.clear();display::setupZero(1000+step*100-1);assert(packets.empty());
    display::setupZero(1000+step*100);assert(packets.size()==2);
    assert(packets[0].selected==chip[(step-1)%6]&&blankPacket(packets[0]));
    assert(packets[1].selected==chip[step%6]&&packets[1].bytes[10]==0xC0);
    for(int digit=1;digit<16;++digit)assert(packets[1].bytes[digit]==(digit==10?0xC0:0x80));
  }
  packets.clear();display::setupZero(1925);assert(packets.size()==2&&packets[1].selected==22); // missed frames skip ahead
  display::blankPrice();packets.clear();display::setupZero(UINT32_MAX-50);assert(packets.size()==1&&packets[0].selected==16);
  packets.clear();display::setupZero(48);assert(packets.empty());display::setupZero(49);assert(packets.size()==2&&blankPacket(packets[0])&&packets[1].selected==17);
  packets.clear();display::price(123456);assert(packets.size()==6); // live prices replace the animation immediately
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
int main(){displayTests();setupDisplayTests();provisioningTests();std::cout<<"PASS: real display transport, setup text/scrolling, 100 ms single-zero chase, and provisioning states with host hardware/network models\n";}
