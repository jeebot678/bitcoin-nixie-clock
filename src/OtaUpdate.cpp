#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "CooperativeTlsClient.h"
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <time.h>
#include <mutex>
#include "ClockCore.h"
#include "MarketParsers.h"
#include "OtaManifest.h"
#include "OtaUpdate.h"
#include "LivePrice.h"

extern const uint8_t otaCaBundle[] asm("_binary_data_cert_x509_crt_bundle_bin_start");
extern const uint8_t otaPublicKey[] asm("_binary_data_cert_ota_public_pem_start");
namespace {
uint32_t nextCheck=60000;
bool pendingBoot=false,confirmed=false;
std::mutex statusMutex;
ota::Status updateStatus;
void stage(ota::Stage value,int httpStatus=0) {
  std::lock_guard<std::mutex> lock(statusMutex);
  updateStatus.stage=value;updateStatus.httpStatus=httpStatus;
}
int openDownload(HTTPClient& http,WiFiClientSecure& tls,std::string url) {
  tls.setCACertBundle(otaCaBundle); tls.setHandshakeTimeout(20);tls.setTimeout(6);
  for (int redirects=0;redirects<5;++redirects) {
    if (!ota::allowedDownload(url)) return -1;
    http.setConnectTimeout(6000);http.setTimeout(6000);http.setReuse(false);http.useHTTP10(true);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(tls,url.c_str())) return -1;
    http.setUserAgent("BitcoinClock-OTA/1.0");http.addHeader("Accept-Encoding","identity");
    http.addHeader("Cache-Control","no-cache");
    const char* headers[]={"Location","Transfer-Encoding","Content-Encoding"};http.collectHeaders(headers,3);
    int status=http.GET();
    if (status!=301&&status!=302&&status!=303&&status!=307&&status!=308) return status;
    String location=http.header("Location");http.end();tls.stop();
    url=location.c_str(); // Only absolute HTTPS redirects to approved GitHub hosts.
  }
  return -1;
}
bool verify(const ota::Manifest& manifest) {
  uint8_t signature[256],hash[32];size_t count=0;
  if (mbedtls_base64_decode(signature,sizeof(signature),&count,(const uint8_t*)manifest.signature.data(),manifest.signature.size())||count!=256) return false;
  std::string payload=ota::canonical(manifest);
  if (mbedtls_sha256_ret((const uint8_t*)payload.data(),payload.size(),hash,0)) return false;
  mbedtls_pk_context key;mbedtls_pk_init(&key);
  int error=mbedtls_pk_parse_public_key(&key,otaPublicKey,strlen((const char*)otaPublicKey)+1);
  if (!error && mbedtls_pk_get_bitlen(&key)!=2048) error=-1;
  if (!error) error=mbedtls_pk_verify(&key,MBEDTLS_MD_SHA256,hash,32,signature,count);
  mbedtls_pk_free(&key);return !error;
}
bool readManifest(ota::Manifest& manifest,uint32_t slotSize) {
  CooperativeTlsClient tls;HTTPClient http;
  std::string url=std::string("https://raw.githubusercontent.com/")+ota_config::kRepository+"/"+ota_config::kBranch+"/ota/manifest.json";
  int status=openDownload(http,tls,url);
  stage(ota::Stage::Manifest,status);
  bool ok=false;
  if (status==200 && http.getSize()>0 && http.getSize()<=4096 && http.header("Transfer-Encoding").isEmpty() && (http.header("Content-Encoding").isEmpty() || http.header("Content-Encoding")=="identity")) {
    String body=http.getString();
    if (body.length()==size_t(http.getSize())) {
      StaticJsonDocument<4096> doc;
      if (!deserializeJson(doc,body.c_str(),body.length())) ok=ota::parseManifest(doc.as<JsonVariantConst>(),ota_config::kRepository,slotSize,manifest)&&verify(manifest);
    }
  }
  http.end();tls.stop();
  if (!ok) {stage(ota::Stage::ManifestRejected,status);Serial.printf("OTA manifest unavailable or rejected (HTTP %d)\n",status);}
  return ok;
}
bool install(const ota::Manifest& manifest) {
  CooperativeTlsClient tls;HTTPClient http;
  int status=openDownload(http,tls,manifest.url);
  stage(ota::Stage::Download,status);
  if (status!=200 || http.getSize()!=int(manifest.size) || !http.header("Transfer-Encoding").isEmpty() || (!http.header("Content-Encoding").isEmpty() && http.header("Content-Encoding")!="identity") || !Update.begin(manifest.size,U_FLASH)) {
    stage(ota::Stage::DownloadRejected,status);http.end();tls.stop();Serial.println("OTA download/header/partition rejected");return false;
  }
  mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);mbedtls_sha256_starts_ret(&sha,0);
  uint8_t buffer[2048];uint32_t received=0,lastData=millis(),started=lastData;
  WiFiClient* stream=http.getStreamPtr();bool ok=true;
  while (received<manifest.size) {
    if (WiFi.status()!=WL_CONNECTED || uint32_t(millis()-lastData)>15000 || uint32_t(millis()-started)>180000) { ok=false;break; }
    int available=stream->available();
    if (!available) { if (!stream->connected()) { ok=false;break; } delay(2);continue; }
    size_t amount=std::min<size_t>(sizeof(buffer),std::min<uint32_t>(available,manifest.size-received));
    int count=stream->read(buffer,amount);
    if (count<=0) { delay(2);continue; }
    if (mbedtls_sha256_update_ret(&sha,buffer,count)||Update.write(buffer,count)!=size_t(count)) { ok=false;break; }
    received+=count;lastData=millis();
    {std::lock_guard<std::mutex> lock(statusMutex);updateStatus.received=received;}
    delay(1);
  }
  uint8_t hash[32]={};char hex[65];
  if (mbedtls_sha256_finish_ret(&sha,hash)) ok=false;
  mbedtls_sha256_free(&sha);
  for (size_t i=0;i<32;++i) snprintf(hex+i*2,3,"%02x",hash[i]);
  if (manifest.sha256!=hex || received!=manifest.size) ok=false;
  // Never activate the new slot before both signature and full image hash pass.
  if (ok) ok=Update.end(false)&&Update.isFinished();else Update.abort();
  http.end();tls.stop();
  if (!ok) {stage(ota::Stage::DownloadFailed,status);Update.abort();Serial.println("OTA failed; current firmware retained");}
  return ok;
}
}
namespace ota {
Status status(){std::lock_guard<std::mutex> lock(statusMutex);return updateStatus;}
bool bootConfirmed(){return confirmed;}
void begin() {
  esp_ota_img_states_t state;
  pendingBoot=esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY;
  confirmed=!pendingBoot;
  nextCheck=60000+(esp_random()%30000);
  Serial.printf("Firmware %s; OTA %s\n",ota_config::kVersion,*ota_config::kRepository?ota_config::kRepository:"awaiting GitHub repository configuration");
}
void healthCheck(uint32_t now,bool healthy) {
  if (!pendingBoot||confirmed) return;
  if (now>=30000&&healthy&&esp_ota_mark_app_valid_cancel_rollback()==ESP_OK) {
    confirmed=true;
    Preferences prefs;prefs.begin("btc-ota",false);prefs.putString("highest",ota_config::kVersion);prefs.end();
    Serial.println("OTA boot healthy; rollback cancelled");
  } else if (now>=90000) {
    Serial.println("OTA boot health failed; rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}
bool checkDue(uint32_t now,bool online) { return confirmed&&online&&*ota_config::kRepository&&btc::due(now,nextCheck); }
void defer(uint32_t now) { nextCheck=now+ota_config::kCheckIntervalMs+(esp_random()%900000); }
bool checkAndInstall() {
  {std::lock_guard<std::mutex> lock(statusMutex);updateStatus=Status{};updateStatus.checkedAt=millis();updateStatus.stage=Stage::Manifest;}
  const esp_partition_t* slot=esp_ota_get_next_update_partition(nullptr);
  if (!slot) {stage(Stage::Unavailable);Serial.println("OTA unavailable: no second application slot");return false;}
  Manifest manifest;
  if (!readManifest(manifest,slot->size)) return false;
  {std::lock_guard<std::mutex> lock(statusMutex);snprintf(updateStatus.candidate,sizeof(updateStatus.candidate),"%s",manifest.version.c_str());updateStatus.total=manifest.size;}
  if (!newer(manifest.version.c_str(),ota_config::kVersion)) {stage(Stage::Current);Serial.println("OTA: firmware is current");return true;}
  Preferences prefs;prefs.begin("btc-ota",false);
  String highest=prefs.getString("highest",ota_config::kVersion);
  if (!newer(manifest.version.c_str(),highest.c_str())) {stage(Stage::AlreadyInstalled);prefs.end();return true;}
  String attempted=prefs.getString("attempt","");uint32_t attemptedAt=prefs.getUInt("attempt-time",0),now=uint32_t(time(nullptr));
  // A crashed image that rolled back is retried at most once per day.
  if (attempted==manifest.version.c_str() && uint64_t(attemptedAt)+86400>now) {
    {std::lock_guard<std::mutex> lock(statusMutex);updateStatus.stage=Stage::RetryLimited;updateStatus.retryAfter=uint64_t(attemptedAt)+86400-now;}
    prefs.end();return true;
  }
  // Routine manifest checks keep the primary price feed open. Only an
  // authenticated new image needs both sockets' memory for installation.
  stage(Stage::Pausing);
  if (!live::pauseAndWait(25000)) {stage(Stage::PauseFailed);prefs.end();live::resume();return false;}
  prefs.putString("attempt",manifest.version.c_str());prefs.putUInt("attempt-time",now);prefs.end();
  Serial.printf("OTA installing signed firmware %s (%lu bytes)\n",manifest.version.c_str(),(unsigned long)manifest.size);
  if (!install(manifest)) {
    // A rejected/interrupted download never activated an image. Clear its
    // attempt marker so a remote retry can recover immediately. Leave the
    // marker intact after activation: an image that crashes and rolls back
    // still receives the existing one-day boot-loop protection.
    prefs.begin("btc-ota",false);prefs.remove("attempt");prefs.remove("attempt-time");prefs.end();
    live::resume();return false;
  }
  stage(Stage::Installed);
  Serial.println("OTA verified and installed; rebooting");delay(300);ESP.restart();return true;
}
}
