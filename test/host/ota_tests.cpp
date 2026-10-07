#include <cassert>
#include <fstream>
#include <sstream>
#include <iostream>
#include "Arduino.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "Update.h"
#include "esp_ota_ops.h"
#include "OtaManifest.h"
SerialMock Serial;EspMock ESP;WifiMock WiFi;UpdateMock Update;uint32_t testMillis=100;
std::map<int,int>pins,modes,adc;std::vector<GpioEvent>gpioEvents;
int bootState=ESP_OTA_IMG_VALID,bootConfirms=0,bootRollbacks=0;
std::string readFile(const std::string&path){std::ifstream f(path,std::ios::binary);assert(f.good());std::ostringstream s;s<<f.rdbuf();return s.str();}
#include "../../src/OtaUpdate.cpp"
#include "key_fixture.h"
int main(int argc,char**argv){assert(argc==2);std::string folder=argv[1];std::string json=readFile(folder+"/manifest.json"),binary=readFile(folder+"/firmware.bin");
  DynamicJsonDocument doc(4096);assert(!deserializeJson(doc,json));ota::Manifest m;assert(ota::parseManifest(doc.as<JsonVariantConst>(),ota_config::kRepository,0x1E0000,m));assert(verify(m));
  ota::Manifest bad=m;bad.version="0.0.0";assert(!verify(bad));bad=m;bad.sha256[0]=bad.sha256[0]=='a'?'b':'a';assert(!verify(bad));bad=m;bad.signature[50]=bad.signature[50]=='a'?'b':'a';assert(!verify(bad));
  WiFi.connection=WL_CONNECTED;
  HttpResponse firmware;firmware.body.assign(binary.begin(),binary.end());httpResponses()[m.url]=firmware;
  assert(install(m)&&Update.activated&&Update.staging==firmware.body);
  Update={};bad=m;bad.sha256=std::string(64,'0');assert(!install(bad)&&!Update.activated&&Update.aborts>0);
  Update={};firmware.body.pop_back();firmware.declaredSize=m.size;httpResponses()[m.url]=firmware;assert(!install(m)&&!Update.activated);
  firmware.body.assign(binary.begin(),binary.end());firmware.declaredSize=int(m.size)+1;httpResponses()[m.url]=firmware;Update={};assert(!install(m)&&!Update.activated);
  firmware.declaredSize=m.size;httpResponses()[m.url]=firmware;Update={};Update.failWrite=true;assert(!install(m)&&!Update.activated);
  Update={};Update.failEnd=true;assert(!install(m)&&!Update.activated);
  Update={};WiFi.connection=0;assert(!install(m)&&!Update.activated);WiFi.connection=WL_CONNECTED;
  HttpResponse redirect;redirect.status=302;redirect.headers["Location"]="http://github.com/insecure";httpResponses()[m.url]=redirect;Update={};assert(!install(m)&&!Update.activated);
  redirect.headers["Location"]="https://release-assets.githubusercontent.com/test/firmware";httpResponses()[m.url]=redirect;httpResponses()[redirect.headers["Location"].value]=firmware;assert(install(m)&&Update.activated);
  firmware.headers["Transfer-Encoding"]="chunked";httpResponses()[m.url]=firmware;Update={};assert(!install(m)&&!Update.activated);
  HttpResponse manifestResponse;manifestResponse.body.assign(json.begin(),json.end());std::string manifestUrl=std::string("https://raw.githubusercontent.com/")+ota_config::kRepository+"/main/ota/manifest.json";httpResponses()[manifestUrl]=manifestResponse;
  ota::Manifest parsed;assert(readManifest(parsed,0x1E0000));manifestResponse.body[0]='[';httpResponses()[manifestUrl]=manifestResponse;assert(!readManifest(parsed,0x1E0000));
  // Exercise the actual public-check/install path, including persisted retry
  // suppression, downgrade protection and a correctly signed current version.
  manifestResponse.body.assign(json.begin(),json.end());httpResponses()[manifestUrl]=manifestResponse;
  firmware.headers.clear();httpResponses()[m.url]=firmware;prefMock()={};Update={};ESP.restarts=0;
  assert(ota::newer(m.version.c_str(),ota_config::kVersion));
  assert(ota::checkAndInstall()&&Update.activated&&ESP.restarts==1);
  assert(prefMock().strings["btc-ota:attempt"]==m.version.c_str());
  Update={};httpUrls().clear();assert(ota::checkAndInstall()&&!Update.activated&&ESP.restarts==1&&httpUrls().size()==1);
  Preferences prefs;prefs.begin("btc-ota",false);prefs.putUInt("attempt-time",uint32_t(time(nullptr))-86401);
  assert(ota::checkAndInstall()&&Update.activated&&ESP.restarts==2);
  prefs.putString("highest",m.version.c_str());Update={};httpUrls().clear();
  assert(ota::checkAndInstall()&&!Update.activated&&ESP.restarts==2&&httpUrls().size()==1);
  std::string currentJson=readFile(folder+"/current-manifest.json");manifestResponse.body.assign(currentJson.begin(),currentJson.end());httpResponses()[manifestUrl]=manifestResponse;
  prefMock()={};httpUrls().clear();assert(ota::checkAndInstall()&&!Update.activated&&httpUrls().size()==1);
  ota::begin();assert(!ota::checkDue(59999,true)&&ota::checkDue(90000,true)&&!ota::checkDue(90000,false));
  uint32_t nearWrap=UINT32_MAX-100000;ota::defer(nearWrap);
  assert(!ota::checkDue(nearWrap+ota_config::kCheckIntervalMs-1,true));assert(ota::checkDue(nearWrap+ota_config::kCheckIntervalMs+900000,true));
  bootState=ESP_OTA_IMG_PENDING_VERIFY;ota::begin();ota::healthCheck(29999,true);assert(!bootConfirms);ota::healthCheck(30000,true);assert(bootConfirms==1);ota::healthCheck(90000,false);assert(!bootRollbacks);
  assert(prefMock().strings["btc-ota:highest"]==ota_config::kVersion);
  bootState=ESP_OTA_IMG_PENDING_VERIFY;ota::begin();ota::healthCheck(90000,false);assert(bootRollbacks==1);
  std::cout<<"PASS: real OTA code; signatures, checksums, interruption, flash failure, redirects, public check/install, daily retry, downgrade/current version, timer rollover, boot confirmation and rollback\n";
}
