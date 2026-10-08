#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>
#include "Provisioning.h"
#include "ClockCore.h"
#include "WifiCredentials.h"

namespace {
String randomToken() {
  char buffer[33]; snprintf(buffer,sizeof(buffer),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
  return String(buffer);
}
const char kPage[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Bitcoin Clock · Wi-Fi setup</title>
<style>body{font:17px system-ui;background:#131619;color:#f2f3f4;margin:0;padding:32px 20px}main{max-width:440px;margin:auto}h1{font-size:28px}label{display:block;margin-top:24px}input,button{box-sizing:border-box;width:100%;padding:14px;border-radius:8px;border:1px solid #667079;font:inherit}input{background:#20262c;color:white;margin-top:8px}button{background:#ffc16b;color:#15181b;font-weight:650;margin-top:26px;border:0}button:disabled{opacity:.6}p{line-height:1.5}#message{min-height:52px}.muted{color:#b6bdc4;font-size:15px}</style>
<main><h1>Connect your Bitcoin Clock</h1><p>Choose a 2.4 GHz Wi-Fi network. Once connected, the clock will close this setup network and display live Bitcoin prices.</p>
<form id="wifi" method="post" action="/connect"><input type="hidden" name="token" value="{{TOKEN}}"><label for="ssid">Wi-Fi network</label><input id="ssid" name="ssid" list="networks" maxlength="32" required autocomplete="off"><datalist id="networks"></datalist>
<label for="password">Wi-Fi password</label><input id="password" name="password" type="password" maxlength="64" autocomplete="new-password"><p class="muted">Leave the password empty for an open network. Hidden networks can be typed manually.</p><button id="connect" type="submit">Connect</button></form><p id="message" role="status" aria-live="polite"></p></main>
<script>const form=document.getElementById('wifi'),message=document.getElementById('message'),button=document.getElementById('connect');let waiting=false;
form.addEventListener('submit',async e=>{e.preventDefault();button.disabled=true;message.textContent='Connecting…';try{const r=await fetch('/connect',{method:'POST',body:new URLSearchParams(new FormData(form))});const d=await r.json();message.textContent=d.message;waiting=r.ok;if(!r.ok)button.disabled=false;}catch(e){message.textContent='Reconnect to the clock setup network and try again.';button.disabled=false;}});
setInterval(async()=>{if(!waiting)return;try{const d=await(await fetch('/status',{cache:'no-store'})).json();message.textContent=d.message;if(d.connected){waiting=false;message.textContent='Connected! Your clock is now fetching live Bitcoin prices. You can close this page.';}else if(!d.connecting){waiting=false;button.disabled=false;}}catch(e){}},1000);
async function scan(){try{const d=await(await fetch('/networks')).json();if(d.scanning){setTimeout(scan,1500);return;}for(const network of d.networks){const o=document.createElement('option');o.value=network.ssid;document.getElementById('networks').append(o);}}catch(e){}}scan();</script></html>)HTML";
}
bool Provisioning::online() const { return WiFi.status()==WL_CONNECTED; }
void Provisioning::begin() {
  prefs_.begin("btc-wifi",false);
  btc::WifiCredentials record;
  if (prefs_.getBytesLength("credentials")==sizeof(record) && prefs_.getBytes("credentials",&record,sizeof(record))==sizeof(record) && btc::validStoredCredentials(record)) {
    savedSsid_=record.ssid; savedPassword_=record.password;
  }
  prefs_.remove("setup-key"); // Discard the legacy setup AP password on upgrade.
  WiFi.persistent(false); WiFi.setAutoReconnect(false); WiFi.mode(WIFI_STA); WiFi.setHostname("bitcoin-clock");
  if (btc::validCredentials(savedSsid_.c_str(),savedPassword_.c_str())) connect(savedSsid_,savedPassword_,false);
  else { savedSsid_=""; startPortal(); }
}
void Provisioning::connect(const String& ssid,const String& password,bool pending) {
  WiFi.disconnect(false,false); WiFi.begin(ssid.c_str(),password.c_str());
  trying_=true; pending_=pending; attemptAt_=millis(); closeAt_=0;
  message_="Connecting to your Wi-Fi…";
}
void Provisioning::startPortal() {
  if (portal_) return;
  WiFi.mode(WIFI_AP_STA); WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  char name[40]; snprintf(name,sizeof(name),"BitcoinClock-%06lx",(unsigned long)(ESP.getEfuseMac()&0xFFFFFF));
  if (!WiFi.softAP(name,nullptr,1,0,2)) { Serial.println("Could not start setup AP"); return; }
  token_=randomToken();
  const char* headers[]={"Origin"}; web_.collectHeaders(headers,1);
  web_.on("/",HTTP_GET,[this]{root();});
  web_.on("/connect",HTTP_POST,[this]{credentials();});
  web_.on("/status",HTTP_GET,[this]{status();});
  web_.on("/networks",HTTP_GET,[this]{networks();});
  web_.onNotFound([this]{web_.sendHeader("Location","http://192.168.4.1/",true);web_.send(302,"text/plain","");});
  dns_.start(53,"*",WiFi.softAPIP()); web_.begin(); portal_=true;
  message_="Choose your Wi-Fi network.";
  Serial.printf("Wi-Fi setup AP: %s (no password)\nOpen http://192.168.4.1\n",name);
}
void Provisioning::stopPortal() {
  web_.stop(); dns_.stop(); WiFi.scanDelete(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);
  portal_=false; closeAt_=0; token_="";
  Serial.println("Connected: setup web server, DNS and AP stopped.");
}
void Provisioning::root() {
  String page=FPSTR(kPage); page.replace("{{TOKEN}}",token_);
  web_.sendHeader("Cache-Control","no-store"); web_.sendHeader("X-Content-Type-Options","nosniff");
  web_.send(200,"text/html; charset=utf-8",page);
}
void Provisioning::credentials() {
  String origin=web_.header("Origin");
  if (web_.arg("token")!=token_ || (!origin.isEmpty() && origin!="http://192.168.4.1")) { web_.send(403,"application/json","{\"message\":\"Reload the setup page and try again.\"}"); return; }
  if (pending_ || online()) { web_.send(409,"application/json","{\"message\":\"A connection is already in progress.\"}"); return; }
  String ssid=web_.arg("ssid"),password=web_.arg("password");
  if (!btc::validCredentials(ssid.c_str(),password.c_str()) || ssid.length()!=strlen(ssid.c_str()) || password.length()!=strlen(password.c_str())) { web_.send(400,"application/json","{\"message\":\"Enter a network name of up to 32 bytes and a valid Wi-Fi password (8–63 characters or 64 hex digits).\"}"); return; }
  pendingSsid_=ssid; pendingPassword_=password; manual_=true;
  web_.send(202,"application/json","{\"message\":\"Connecting to your Wi-Fi…\"}");
  connect(pendingSsid_,pendingPassword_,true);
}
void Provisioning::status() {
  StaticJsonDocument<384> doc; doc["connected"]=online()&&!pending_; doc["connecting"]=trying_; doc["message"]=message_;
  String json; serializeJson(doc,json); web_.sendHeader("Cache-Control","no-store"); web_.send(200,"application/json",json);
}
void Provisioning::networks() {
  int count=WiFi.scanComplete();
  if (count==WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); web_.send(200,"application/json","{\"scanning\":true}"); return; }
  if (count==WIFI_SCAN_RUNNING) { web_.send(200,"application/json","{\"scanning\":true}"); return; }
  DynamicJsonDocument doc(4096); JsonArray networks=doc.createNestedArray("networks");
  for (int i=0;i<count&&i<20;++i) { JsonObject item=networks.createNestedObject(); item["ssid"]=WiFi.SSID(i); item["rssi"]=WiFi.RSSI(i); }
  String json; serializeJson(doc,json); web_.send(200,"application/json",json);
}
void Provisioning::openSetup() {
  WiFi.disconnect(false,false); trying_=pending_=wasOnline_=false; manual_=true;
  pendingSsid_=""; pendingPassword_=""; closeAt_=0; startPortal();
}
void Provisioning::forget() {
  prefs_.remove("credentials"); savedSsid_=""; savedPassword_=""; openSetup();
}
void Provisioning::loop(uint32_t now) {
  if (portal_) { dns_.processNextRequest(); web_.handleClient(); }
  if (online()) {
    if (pending_) {
      // One NVS blob prevents a mixed SSID/password after a power loss.
      btc::WifiCredentials record;
      pendingSsid_.toCharArray(record.ssid,sizeof(record.ssid));
      pendingPassword_.toCharArray(record.password,sizeof(record.password));
      if (prefs_.putBytes("credentials",&record,sizeof(record))!=sizeof(record)) {
        WiFi.disconnect(false,false); trying_=pending_=false;
        pendingSsid_=""; pendingPassword_="";
        message_="Could not save Wi-Fi. Please try connecting again.";
        return;
      }
      savedSsid_=pendingSsid_; savedPassword_=pendingPassword_; pendingSsid_=""; pendingPassword_=""; pending_=false;
    }
    if (!wasOnline_) { Serial.printf("Wi-Fi connected, IP %s\n",WiFi.localIP().toString().c_str()); wasOnline_=true; }
    trying_=false; manual_=false; disconnectedAt_=0; message_="Connected! Setup will close automatically.";
    if (portal_ && !closeAt_) closeAt_=now+8000;
    if (portal_ && btc::due(now,closeAt_)) stopPortal();
    return;
  }
  closeAt_=0;
  if (wasOnline_) { wasOnline_=false; disconnectedAt_=now; retryAt_=now; }
  if (trying_ && uint32_t(now-attemptAt_)>=config::kConnectTimeoutMs) {
    trying_=false; WiFi.disconnect(false,false); retryAt_=now+30000;
    if (pending_) { pending_=false; pendingSsid_=""; pendingPassword_=""; message_="Could not connect. Check the password and choose a 2.4 GHz network."; }
    if (!portal_ && !disconnectedAt_) startPortal();
  }
  if (!manual_ && !trying_ && !savedSsid_.isEmpty() && btc::due(now,retryAt_)) connect(savedSsid_,savedPassword_,false);
  if (!manual_ && disconnectedAt_ && uint32_t(now-disconnectedAt_)>=config::kRecoveryApMs) startPortal();
}
