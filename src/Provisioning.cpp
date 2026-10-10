#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <vector>
#include "Provisioning.h"
#include "ClockCore.h"
#include "WifiCredentials.h"

namespace {
String randomToken() {
  char buffer[33]; snprintf(buffer,sizeof(buffer),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
  return String(buffer);
}
const char kPage[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Bitcoin Clock · Wi-Fi setup</title>
<style>body{font:17px system-ui;background:#131619;color:#f2f3f4;margin:0;padding:32px 20px}main{max-width:440px;margin:auto}h1{font-size:28px}label{display:block;margin-top:24px}input,select,button{box-sizing:border-box;width:100%;padding:14px;border-radius:8px;border:1px solid #667079;font:inherit}input,select{background:#20262c;color:white;margin-top:8px}button{background:#ffc16b;color:#15181b;font-weight:650;margin-top:26px;border:0}button:disabled{opacity:.6}button.secondary{background:#20262c;color:#f2f3f4;border:1px solid #667079;margin-top:8px}p{line-height:1.5}#message{min-height:52px}.muted{color:#b6bdc4;font-size:15px}[hidden]{display:none!important}</style>
<main><h1>Connect your Bitcoin Clock</h1><p>Choose a 2.4 GHz Wi-Fi network. Once connected, the clock will close this setup network and display live Bitcoin prices.</p>
<form id="wifi" method="post" action="/connect"><input id="token" type="hidden" name="token" value="{{TOKEN}}"><input id="ssid" type="hidden" name="ssid">
<label for="network">Wi-Fi network</label><select id="network" required disabled><option value="">Searching for networks…</option></select><p id="scan-message" class="muted" role="status" aria-live="polite">Searching for nearby 2.4 GHz Wi-Fi networks…</p><button id="rescan" class="secondary" type="button" disabled>Scan again</button>
<div id="manual" hidden><label for="manual-ssid">Network name</label><input id="manual-ssid" maxlength="32" autocomplete="off"><p class="muted">Enter the exact name of a hidden or unlisted network.</p></div>
<div id="password-field" hidden><label for="password">Wi-Fi password</label><input id="password" name="password" type="password" minlength="8" maxlength="64" autocomplete="new-password" disabled><p class="muted">The clock remembers your Wi-Fi after it connects. No username is needed.</p></div>
<button id="connect" type="submit" disabled>Connect</button></form><p id="message" role="status" aria-live="polite"></p></main>
<script>const byId=id=>document.getElementById(id),form=byId('wifi'),message=byId('message'),button=byId('connect'),picker=byId('network'),ssid=byId('ssid'),password=byId('password'),manual=byId('manual'),manualSsid=byId('manual-ssid'),passwordField=byId('password-field'),rescan=byId('rescan'),scanMessage=byId('scan-message');let waiting=false,completed=false,scanning=true,scanTimer=null,networks=[],secured=false;
function controls(){const locked=waiting||completed;picker.disabled=locked||scanning;rescan.disabled=locked||scanning;manualSsid.disabled=locked;password.disabled=locked||!secured;button.disabled=locked||scanning||!ssid.value;}
function choose(clearPassword=true){const hidden=picker.value==='manual',network=networks[Number(picker.value.slice(1))];manual.hidden=!hidden;manualSsid.required=hidden;ssid.value=hidden?manualSsid.value:(picker.value&&network?network.ssid:'');secured=hidden||!!(ssid.value&&network&&network.secured);passwordField.hidden=!secured;password.required=secured;if(clearPassword||!secured)password.value='';controls();}
picker.addEventListener('change',()=>choose());manualSsid.addEventListener('input',()=>choose(false));
function option(label,value){const o=document.createElement('option');o.textContent=label;o.value=value;picker.append(o);}
async function scan(refresh=false){if(waiting||completed)return;scanning=true;controls();scanMessage.textContent='Searching for nearby 2.4 GHz Wi-Fi networks…';try{const r=await fetch('/networks'+(refresh?'?refresh=1':''),{cache:'no-store'}),d=await r.json();if(!r.ok)throw new Error(d.message||'Scan failed. Try again.');if(waiting||completed)return;if(d.scanning||d.busy){scanTimer=setTimeout(()=>scan(),1200);return;}const previous=ssid.value,wasManual=picker.value==='manual';networks=d.networks;picker.replaceChildren();option('Choose a network','');for(let i=0;i<networks.length;i++){const n=networks[i],strength=n.rssi>=-55?'Strong':n.rssi>=-70?'Good':'Weak';option(n.ssid+' · '+strength+' signal'+(n.secured?'':' · open'),'n'+i);}option('Other network…','manual');const index=networks.findIndex(n=>n.ssid===previous);picker.value=wasManual?'manual':index>=0?'n'+index:'';scanning=false;choose(false);scanMessage.textContent=networks.length?'Choose your network above. Open networks need no password.':'No networks found. Move closer to your router or choose Other network.';}catch(e){if(waiting||completed)return;scanning=false;if(!picker.options.length||picker.options[0].textContent==='Searching for networks…'){picker.replaceChildren();option('Choose a network','');option('Other network…','manual');picker.value='';choose(false);}scanMessage.textContent=e.message||'Could not scan. Reconnect to the clock setup network and try again.';controls();}}
rescan.addEventListener('click',()=>{clearTimeout(scanTimer);scan(true);});
form.addEventListener('submit',async e=>{e.preventDefault();if(waiting||completed)return;choose(false);if(!form.reportValidity())return;const body=new URLSearchParams(new FormData(form));waiting=true;clearTimeout(scanTimer);controls();message.textContent='Connecting…';try{const r=await fetch('/connect',{method:'POST',body}),d=await r.json();message.textContent=d.message;waiting=r.ok;controls();}catch(e){waiting=false;message.textContent='Reconnect to the clock setup network and try again.';controls();}});
setInterval(async()=>{if(!waiting)return;try{const d=await(await fetch('/status',{cache:'no-store'})).json();message.textContent=d.message;if(d.connected){waiting=false;completed=true;message.textContent='Connected! Your clock is now fetching live Bitcoin prices. You can close this page.';controls();}else if(!d.connecting){waiting=false;controls();}}catch(e){}},1000);scan();</script></html>)HTML";
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
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN); WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  static bool diagnosticsRegistered=false;
  if (!diagnosticsRegistered) {
    WiFi.onEvent([](WiFiEvent_t,WiFiEventInfo_t info){Serial.printf("Wi-Fi disconnect reason=%u\n",unsigned(info.wifi_sta_disconnected.reason));},ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    diagnosticsRegistered=true;
  }
  if (btc::validCredentials(savedSsid_.c_str(),savedPassword_.c_str())) connect(savedSsid_,savedPassword_,false);
  else { savedSsid_=""; startPortal(); }
}
void Provisioning::connect(const String& ssid,const String& password,bool pending) {
  // Finish the setup scan before requesting association on the same radio.
  if (WiFi.scanComplete()==WIFI_SCAN_RUNNING) esp_wifi_scan_stop();
  WiFi.scanDelete();
  WiFi.setMinSecurity(password.isEmpty()?WIFI_AUTH_OPEN:WIFI_AUTH_WPA2_PSK);
  WiFi.disconnect(false,false); WiFi.begin(ssid.c_str(),password.c_str());
  trying_=true; pending_=pending; attemptAt_=millis(); closeAt_=0;
  setupState_=btc::WifiSetupState::Connecting;
  message_="Connecting to your Wi-Fi…";
}
void Provisioning::startPortal() {
  if (portal_) return;
  WiFi.mode(WIFI_AP_STA); WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  char name[40]; snprintf(name,sizeof(name),"BitcoinClock-%06lx",(unsigned long)(ESP.getEfuseMac()&0xFFFFFF));
  if (!WiFi.softAP(name,nullptr,1,0,2)) { setupState_=btc::WifiSetupState::ApFailed; Serial.println("Could not start setup AP"); return; }
  token_=randomToken();
  const char* headers[]={"Origin"}; web_.collectHeaders(headers,1);
  web_.on("/",HTTP_GET,[this]{root();});
  web_.on("/connect",HTTP_POST,[this]{credentials();});
  web_.on("/status",HTTP_GET,[this]{status();});
  web_.on("/networks",HTTP_GET,[this]{networks();});
  web_.onNotFound([this]{web_.sendHeader("Location","http://192.168.4.1/",true);web_.send(302,"text/plain","");});
  dns_.start(53,"*",WiFi.softAPIP()); web_.begin(); portal_=true;
  setupState_=trying_?btc::WifiSetupState::Connecting:btc::WifiSetupState::Waiting;
  message_=trying_?"Connecting to your Wi-Fi…":"Choose your Wi-Fi network.";
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
  web_.sendHeader("Cache-Control","no-store");
  if (trying_ || online()) { web_.send(200,"application/json","{\"busy\":true,\"networks\":[]}"); return; }
  int count=WiFi.scanComplete();
  if (web_.arg("refresh")=="1" && count!=WIFI_SCAN_RUNNING) { WiFi.scanDelete(); count=WIFI_SCAN_FAILED; }
  if (count==WIFI_SCAN_FAILED) count=WiFi.scanNetworks(true,true);
  if (count==WIFI_SCAN_RUNNING) { web_.send(200,"application/json","{\"scanning\":true}"); return; }
  if (count<0) { web_.send(503,"application/json","{\"message\":\"Could not scan for Wi-Fi. Try scanning again.\"}"); return; }
  std::vector<int> order; order.reserve(count);
  for (int i=0;i<count;++i) if (!WiFi.SSID(i).isEmpty()) order.push_back(i);
  std::sort(order.begin(),order.end(),[](int a,int b){return WiFi.RSSI(a)>WiFi.RSSI(b);});
  DynamicJsonDocument doc(JSON_OBJECT_SIZE(1)+JSON_ARRAY_SIZE(count)+size_t(count)*(JSON_OBJECT_SIZE(4)+34));
  JsonArray networks=doc.createNestedArray("networks");
  for (int index:order) {
    String name=WiFi.SSID(index); bool duplicate=false;
    for (JsonObject item:networks) if (name==item["ssid"].as<const char*>()) {duplicate=true;break;}
    if (duplicate) continue;
    JsonObject item=networks.createNestedObject(); item["ssid"]=name; item["rssi"]=WiFi.RSSI(index);
    item["secured"]=WiFi.encryptionType(index)!=WIFI_AUTH_OPEN; item["channel"]=WiFi.channel(index);
  }
  if (doc.overflowed()) {web_.send(503,"application/json","{\"message\":\"Too many networks to list. Try scanning again or enter a network name.\"}");return;}
  String json; serializeJson(doc,json); web_.send(200,"application/json",json);
}
void Provisioning::openSetup() {
  WiFi.disconnect(false,false); trying_=pending_=wasOnline_=false; manual_=true;
  pendingSsid_=""; pendingPassword_=""; closeAt_=0; startPortal();
  setupState_=portal_?btc::WifiSetupState::Waiting:btc::WifiSetupState::ApFailed;
  message_="Choose your Wi-Fi network.";
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
        setupState_=btc::WifiSetupState::SaveFailed;
        message_="Could not save Wi-Fi. Please try connecting again.";
        return;
      }
      savedSsid_=pendingSsid_; savedPassword_=pendingPassword_; pendingSsid_=""; pendingPassword_=""; pending_=false;
    }
    if (!wasOnline_) { Serial.printf("Wi-Fi connected, IP %s\n",WiFi.localIP().toString().c_str()); wasOnline_=true; }
    trying_=false; manual_=false; disconnectedAt_=0; message_="Connected! Setup will close automatically.";
    setupState_=btc::WifiSetupState::Connected;
    if (portal_ && !closeAt_) closeAt_=now+8000;
    if (portal_ && btc::due(now,closeAt_)) stopPortal();
    return;
  }
  closeAt_=0;
  if (wasOnline_) { wasOnline_=false; disconnectedAt_=now; retryAt_=now; }
  if (trying_ && uint32_t(now-attemptAt_)>=config::kConnectTimeoutMs) {
    trying_=false; WiFi.disconnect(false,false); retryAt_=now+30000;
    if (pending_) { pending_=false; pendingSsid_=""; pendingPassword_=""; }
    if (!portal_ && !disconnectedAt_) startPortal();
    if (setupState_!=btc::WifiSetupState::ApFailed) setupState_=btc::WifiSetupState::ConnectionFailed;
    message_="Could not connect. Check the password and choose a 2.4 GHz network.";
  }
  if (!manual_ && !trying_ && !savedSsid_.isEmpty() && btc::due(now,retryAt_)) connect(savedSsid_,savedPassword_,false);
  if (!manual_ && disconnectedAt_ && uint32_t(now-disconnectedAt_)>=config::kRecoveryApMs) startPortal();
}
