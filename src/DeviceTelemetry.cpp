#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "DeviceTelemetry.h"
#include "ClockCore.h"
#include "OtaConfig.h"

namespace {
QueueHandle_t snapshots=nullptr,commands=nullptr;
telemetry::Snapshot current;
WebServer telemetryWeb(8080);
char controlToken[33]={};
bool listening=false,mdns=false;
uint32_t stackRemaining=0;
StaticJsonDocument<JSON_OBJECT_SIZE(61)+2*JSON_OBJECT_SIZE(7)+2*JSON_ARRAY_SIZE(5)+JSON_OBJECT_SIZE(7)+280> telemetryJson;
const char kTelemetryPage[] PROGMEM=R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Bitcoin Clock diagnostics</title>
<style>body{font:16px system-ui;background:#15191c;color:#eee;max-width:760px;margin:30px auto;padding:0 18px}table{width:100%;border-collapse:collapse}td,th{padding:10px;text-align:left;border-bottom:1px solid #555}button{padding:12px;margin:6px 4px 6px 0;font:inherit}pre{white-space:pre-wrap}#message{min-height:24px}</style>
<h1>Clock diagnostics</h1><p>Rotate one switch at a time. A held/default setting with an invalid input can explain a frozen display. Voltages are measured at the ESP32 inputs.</p>
<table><thead><tr><th>Control</th><th>Input</th><th>Selected setting</th><th>Reading</th></tr></thead><tbody id="dials"></tbody></table>
<p id="status">Connecting…</p><button data-action="readings">Show voltages on matrix</button><button data-action="chart">Restore chart</button><button data-action="tubes">Test tubes for 5 seconds</button><button data-action="cadence">Test live price at 500 ms for 30 seconds</button><button data-action="ota">Check for update now</button><p id="message" role="status"></p><pre id="details"></pre>
<script>let token=null;const labels={frequency:['0.1 s','0.5 s','1.5 s','10 s','1 min'],range:['30 s','1 min','5 min','30 min','1 h']};
async function request(path,options={}){const r=await fetch(path,{cache:'no-store',...options});const data=await r.json();if(!r.ok)throw new Error(data.message||'Request failed');return data;}
async function refresh(){try{const s=await request('/telemetry'),rows=[];for(const kind of ['frequency','range']){const d=s.dials[kind],tr=document.createElement('tr');for(const text of [kind==='frequency'?'Frequency · JSEL2/GPIO35':'Range · JSEL1/GPIO34',d.mv+' mV / ADC '+d.raw,labels[kind][d.selected]+(d.initialized?'':' (default)'),d.decoded<0?'Invalid input; previous setting held':!d.initialized||d.decoded!==d.selected?'Changing / awaiting debounce':'Stable · position '+(d.selected+1)]){const td=document.createElement('td');td.textContent=text;tr.append(td);}rows.push(tr);}document.getElementById('dials').replaceChildren(...rows);document.getElementById('status').textContent='Firmware '+s.firmware+' · '+s.streams+' streams · '+s.accepted_quotes+' accepted quotes · '+s.nixie_frames+' tube commands';document.getElementById('details').textContent=JSON.stringify(s,null,2);}catch(e){document.getElementById('status').textContent='Disconnected / restarting: '+e.message;}setTimeout(refresh,500);}
document.querySelectorAll('button').forEach(button=>button.onclick=async()=>{button.disabled=true;try{if(!token)token=(await request('/control-token')).token;const data=await request('/control',{method:'POST',body:new URLSearchParams({token,action:button.dataset.action})});document.getElementById('message').textContent=data.message;}catch(e){token=null;document.getElementById('message').textContent=e.message;}finally{button.disabled=false;}});refresh();</script></html>)HTML";
void noCache(){telemetryWeb.sendHeader("Cache-Control","no-store");telemetryWeb.sendHeader("X-Content-Type-Options","nosniff");}
void writeDial(JsonObject out,const telemetry::Dial& dial,const uint16_t centers[5],unsigned pin) {
  out["pin"]=pin;out["mv"]=dial.mv;out["raw"]=dial.raw;
  out["decoded"]=dial.decoded;out["selected"]=dial.selected;out["initialized"]=dial.initialized;
  JsonArray expected=out.createNestedArray("centers_mv");for(unsigned i=0;i<5;++i)expected.add(centers[i]);
}
void state() {
  telemetryJson.clear();auto& s=current;
  telemetryJson["schema"]=1;telemetryJson["firmware"]=ota_config::kVersion;telemetryJson["board"]=ota_config::kBoard;
  char id[17];snprintf(id,sizeof(id),"%012llx",(unsigned long long)ESP.getEfuseMac());telemetryJson["device_id"]=id;
  telemetryJson["uptime_ms"]=s.uptime;telemetryJson["snapshot_age_ms"]=uint32_t(millis()-s.uptime);
  telemetryJson["wifi"]=s.online;telemetryJson["clock_synced"]=s.clock;telemetryJson["ip"]=WiFi.localIP().toString();
  telemetryJson["heap"]=s.heap;telemetryJson["largest_allocation"]=s.largest;telemetryJson["telemetry_stack_remaining"]=stackRemaining;
  telemetryJson["data_heap"]=s.dataHeap;telemetryJson["data_largest_allocation"]=s.dataBlock;telemetryJson["http_stack_remaining"]=s.httpStack;
  JsonObject dials=telemetryJson.createNestedObject("dials");
  writeDial(dials.createNestedObject("frequency"),s.frequency,config::kRefreshMv,config::kRefreshPin);
  writeDial(dials.createNestedObject("range"),s.range,config::kTimelineMv,config::kTimelinePin);
  telemetryJson["refresh_ms"]=s.interval;telemetryJson["range_seconds"]=s.window;
  telemetryJson["cadence_test"]=s.cadenceTest;
  const char* kinds[]={"quote","history","fx","ota","candles","cache"};
  telemetryJson["http_kind"]=s.httpKind<6?kinds[s.httpKind]:"idle";
  telemetryJson["http_source"]=s.httpSource<btc::kSourceCount?btc::kSources[s.httpSource].name:"none";
  telemetryJson["last_http_status"]=s.httpStatus;telemetryJson["quote_request_pending"]=s.quoteRequested;
  telemetryJson["chart_columns_mask"]=s.chartColumns;telemetryJson["chart_pixels"]=s.chartPixels;
  telemetryJson["chart_type"]="candles";telemetryJson["chart_axis_low_usd"]=s.axisLow;telemetryJson["chart_usd_per_row"]=s.axisStep;
  telemetryJson["history_candles"]=s.historyCandles;telemetryJson["history_saved_epoch"]=s.historySavedAt;
  telemetryJson["streams"]=s.connections;telemetryJson["stream_quotes"]=s.quotes;telemetryJson["accepted_quotes"]=s.accepted;
  telemetryJson["quote_age_ms"]=s.accepted?s.quoteAge:UINT32_MAX;telemetryJson["confirmed"]=s.confirmed;
  telemetryJson["market_price"]=s.marketPrice;telemetryJson["display_quote"]=s.displayPrice;telemetryJson["display_digits"]=s.digits;
  telemetryJson["display_source"]=s.source<btc::kSourceCount?btc::kSources[s.source].name:"unknown";
  telemetryJson["nixie_frames"]=s.frames;telemetryJson["last_frame_gap_ms"]=s.frameGap;
  telemetryJson["last_frame_age_ms"]=s.frames?s.frameAge:UINT32_MAX;telemetryJson["next_frame_in_ms"]=s.nextFrame;
  telemetryJson["min_frame_gap_ms"]=s.minimumGap;telemetryJson["max_frame_gap_ms"]=s.maximumGap;
  telemetryJson["price_transitions"]=s.transitions;telemetryJson["price_changes"]=s.priceChanges;telemetryJson["multi_digit_changes"]=s.multiChanges;
  telemetryJson["http_busy"]=s.inflight;telemetryJson["ota_queued"]=s.otaPending;
  telemetryJson["boot_confirmed"]=s.bootConfirmed;
  JsonObject update=telemetryJson.createNestedObject("ota");
  update["state"]=ota::stageName(s.ota.stage);update["http_status"]=s.ota.httpStatus;
  update["checked_at_ms"]=s.ota.checkedAt;update["candidate_version"]=s.ota.candidate;
  update["received_bytes"]=s.ota.received;update["total_bytes"]=s.ota.total;update["retry_after_seconds"]=s.ota.retryAfter;
  const char* modes[]={"price","startup","blank","tube-test"};
  telemetryJson["display_mode"]=s.displayMode<4?modes[s.displayMode]:"unknown";
  telemetryJson["tube_test_step"]=s.testStep;telemetryJson["matrix_diagnostic_page"]=s.matrixPage;
  telemetryJson["visible_output_verified"]=false; // No MISO/cathode feedback exists.
  String json;serializeJson(telemetryJson,json);noCache();telemetryWeb.send(200,"application/json",json);
}
void control() {
  noCache();String origin=telemetryWeb.header("Origin");
  String ipOrigin=String("http://")+WiFi.localIP().toString()+String(":8080");
  if (telemetryWeb.arg("token")!=controlToken || (!origin.isEmpty() && origin!="http://bitcoin-clock.local:8080" && origin!=ipOrigin)) {
    telemetryWeb.send(403,"application/json","{\"message\":\"Reload diagnostics before sending a command.\"}");return;
  }
  String action=telemetryWeb.arg("action");telemetry::Command command;
  if(action=="ota")command=telemetry::Command::Ota;
  else if(action=="tubes")command=telemetry::Command::Tubes;
  else if(action=="readings")command=telemetry::Command::Readings;
  else if(action=="chart")command=telemetry::Command::Chart;
  else if(action=="cadence")command=telemetry::Command::Cadence;
  else {telemetryWeb.send(400,"application/json","{\"message\":\"Unknown command.\"}");return;}
  if (xQueueSend(commands,&command,0)!=pdTRUE) {telemetryWeb.send(503,"application/json","{\"message\":\"Command queue busy; try again.\"}");return;}
  telemetryWeb.send(202,"application/json",action=="ota"?"{\"message\":\"OTA check queued; signed newer firmware will install automatically.\"}":"{\"message\":\"Command queued.\"}");
}
void step() {
  telemetry::Snapshot snapshot;
  if(xQueueReceive(snapshots,&snapshot,0)==pdTRUE)current=snapshot;
  bool available=current.online&&!current.portal&&WiFi.status()==WL_CONNECTED;
  if(available&&!listening) {
    telemetryWeb.begin();listening=true;mdns=MDNS.begin("bitcoin-clock");
    if(mdns)MDNS.addService("http","tcp",8080);
    Serial.println("Wi-Fi diagnostics: http://bitcoin-clock.local:8080 (or the device IP on port 8080)");
  } else if(!available&&listening) {
    telemetryWeb.stop();if(mdns)MDNS.end();listening=mdns=false;
  }
  if(listening)telemetryWeb.handleClient();
}
void worker(void*) {
  for(;;){step();
#ifdef ARDUINO
    stackRemaining=uxTaskGetStackHighWaterMark(nullptr);
#endif
    delay(20);
  }
}
}
namespace telemetry {
bool begin() {
  snapshots=xQueueCreate(1,sizeof(Snapshot));commands=xQueueCreate(4,sizeof(Command));
  if(!snapshots||!commands){if(snapshots)vQueueDelete(snapshots);if(commands)vQueueDelete(commands);snapshots=commands=nullptr;return false;}
  snprintf(controlToken,sizeof(controlToken),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
  const char* headers[]={"Origin"};telemetryWeb.collectHeaders(headers,1);
  telemetryWeb.on("/",HTTP_GET,[]{noCache();telemetryWeb.send(200,"text/html; charset=utf-8",FPSTR(kTelemetryPage));});
  telemetryWeb.on("/telemetry",HTTP_GET,state);
  telemetryWeb.on("/control-token",HTTP_GET,[]{StaticJsonDocument<96> doc;doc["token"]=controlToken;String json;serializeJson(doc,json);noCache();telemetryWeb.send(200,"application/json",json);});
  telemetryWeb.on("/control",HTTP_POST,control);
  telemetryWeb.onNotFound([]{telemetryWeb.send(404,"application/json","{\"message\":\"Not found.\"}");});
  // Network parsing/sends never execute on the display's core. A slow browser
  // can wait in this task without blocking the 500 ms hardware timer.
  if(xTaskCreatePinnedToCore(worker,"btc-telemetry",6144,nullptr,0,nullptr,0)!=pdPASS){vQueueDelete(snapshots);vQueueDelete(commands);snapshots=commands=nullptr;return false;}
  return true;
}
void publish(const Snapshot& snapshot){if(snapshots)xQueueOverwrite(snapshots,&snapshot);}
bool receive(Command& command){return commands&&xQueueReceive(commands,&command,0)==pdTRUE;}
}
