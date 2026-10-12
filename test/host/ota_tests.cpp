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
unsigned pauses=0,resumes=0;bool canPause=true;
namespace live {bool pauseAndWait(uint32_t timeout){assert(timeout==25000);++pauses;return canPause;}void resume(){++resumes;}}
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
  assert(ota::status().stage==ota::Stage::ManifestRejected&&ota::status().httpStatus==200);
  // Exercise the actual public-check/install path, including persisted retry
  // suppression, downgrade protection and a correctly signed current version.
  manifestResponse.body.assign(json.begin(),json.end());httpResponses()[manifestUrl]=manifestResponse;
  firmware.headers.clear();httpResponses()[m.url]=firmware;prefMock()={};Update={};ESP.restarts=0;
  assert(ota::newer(m.version.c_str(),ota_config::kVersion));
  canPause=false;assert(!ota::checkAndInstall()&&!Update.activated&&resumes==1&&prefMock().strings["btc-ota:attempt"].isEmpty());canPause=true;
  assert(ota::status().stage==ota::Stage::PauseFailed);
  // A failed transport or flash write never activated an image, so an
  // immediate remote retry must remain possible for this same signed version.
  HttpResponse unavailable=firmware;unavailable.status=503;httpResponses()[m.url]=unavailable;
  assert(!ota::checkAndInstall()&&!Update.activated&&prefMock().strings["btc-ota:attempt"].isEmpty());
  auto failure=ota::status();assert(failure.stage==ota::Stage::DownloadRejected&&failure.httpStatus==503&&failure.total==m.size);
  assert(std::string(failure.candidate)==m.version&&failure.checkedAt>0&&failure.received==0);
  httpResponses()[m.url]=firmware;Update.failWrite=true;
  assert(!ota::checkAndInstall()&&!Update.activated&&prefMock().strings["btc-ota:attempt"].isEmpty());
  assert(ota::status().stage==ota::Stage::DownloadFailed&&ota::status().httpStatus==200);
  assert(failure.stage==ota::Stage::DownloadRejected); // Caller owns its copy.
  Update={};
  assert(ota::checkAndInstall()&&Update.activated&&ESP.restarts==1);
  assert(ota::status().stage==ota::Stage::Installed&&ota::status().received==m.size);
  assert(prefMock().strings["btc-ota:attempt"]==m.version.c_str());
  Update={};httpUrls().clear();assert(ota::checkAndInstall()&&!Update.activated&&ESP.restarts==1&&httpUrls().size()==1);
  assert(ota::status().stage==ota::Stage::RetryLimited&&ota::status().retryAfter>0&&ota::status().retryAfter<=86400);
  Preferences prefs;prefs.begin("btc-ota",false);prefs.putUInt("attempt-time",uint32_t(time(nullptr))-86401);
  assert(ota::checkAndInstall()&&Update.activated&&ESP.restarts==2);
  prefs.putString("highest",m.version.c_str());Update={};httpUrls().clear();
  assert(ota::checkAndInstall()&&!Update.activated&&ESP.restarts==2&&httpUrls().size()==1);
  assert(ota::status().stage==ota::Stage::AlreadyInstalled);
  std::string currentJson=readFile(folder+"/current-manifest.json");manifestResponse.body.assign(currentJson.begin(),currentJson.end());httpResponses()[manifestUrl]=manifestResponse;
  unsigned priorPauses=pauses;prefMock()={};httpUrls().clear();assert(ota::checkAndInstall()&&!Update.activated&&httpUrls().size()==1&&pauses==priorPauses);
  assert(ota::status().stage==ota::Stage::Current);
  ota::begin();assert(!ota::checkDue(59999,true)&&ota::checkDue(90000,true)&&!ota::checkDue(90000,false));
  uint32_t nearWrap=UINT32_MAX-100000;ota::defer(nearWrap);
  assert(!ota::checkDue(nearWrap+ota_config::kCheckIntervalMs-1,true));assert(ota::checkDue(nearWrap+ota_config::kCheckIntervalMs+900000,true));
  bootState=ESP_OTA_IMG_PENDING_VERIFY;ota::begin();ota::healthCheck(29999,true);assert(!bootConfirms);ota::healthCheck(30000,true);assert(bootConfirms==1);ota::healthCheck(90000,false);assert(!bootRollbacks);
  assert(prefMock().strings["btc-ota:highest"]==ota_config::kVersion);
  bootState=ESP_OTA_IMG_PENDING_VERIFY;ota::begin();ota::healthCheck(90000,false);assert(bootRollbacks==1);
  std::cout<<"PASS: real OTA code; signatures, checksums, interruption, flash failure, redirects, public check/install, immediate failed-download retry, protected post-activation daily retry, progress/status, downgrade/current version, timer rollover, boot confirmation and rollback\n";
}
