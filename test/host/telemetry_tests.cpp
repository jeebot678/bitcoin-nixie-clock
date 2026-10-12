#include <cassert>
#include <deque>
#include <iostream>
#include "Arduino.h"
#include "WiFi.h"
struct Queue {size_t capacity,size;std::deque<std::vector<uint8_t>> values;Queue(size_t n,size_t s):capacity(n),size(s){}};
using QueueHandle_t=Queue*;constexpr int pdTRUE=1,pdPASS=1;
unsigned queues=0;bool failQueue=false,failTask=false,failSecond=false;
QueueHandle_t xQueueCreate(int count,size_t size){if(failQueue||(failSecond&&queues))return nullptr;++queues;return new Queue(count,size);}
void vQueueDelete(QueueHandle_t q){delete q;--queues;}
int xQueueSend(QueueHandle_t q,const void* p,int timeout){assert(timeout==0);if(q->values.size()==q->capacity)return 0;const auto* bytes=static_cast<const uint8_t*>(p);q->values.emplace_back(bytes,bytes+q->size);return pdTRUE;}
int xQueueOverwrite(QueueHandle_t q,const void* p){assert(q->capacity==1);q->values.clear();return xQueueSend(q,p,0);}
int xQueueReceive(QueueHandle_t q,void* p,int timeout){assert(timeout==0);if(q->values.empty())return 0;memcpy(p,q->values.front().data(),q->size);q->values.pop_front();return pdTRUE;}
int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned stack,void* argument,unsigned priority,void*,int core){assert(stack==6144&&!argument&&priority==0&&core==0);return failTask?0:pdPASS;}
SerialMock Serial;EspMock ESP;WifiMock WiFi;uint32_t testMillis=100;
std::map<int,int>pins,modes,adc;std::vector<GpioEvent>gpioEvents;
#include "../../src/DeviceTelemetry.cpp"
int main(){
  failQueue=true;assert(!telemetry::begin()&&!queues);failQueue=false;
  failSecond=true;assert(!telemetry::begin()&&!queues);failSecond=false;
  failTask=true;assert(!telemetry::begin()&&!queues);failTask=false;
  assert(telemetry::begin()&&queues==2);step();assert(!listening);
  telemetry::Snapshot snapshot;snapshot.online=true;snapshot.clock=true;snapshot.uptime=testMillis;
  snapshot.frequency.mv=309;snapshot.frequency.raw=123;snapshot.frequency.decoded=-1;snapshot.frequency.selected=4;snapshot.frequency.initialized=true;
  snapshot.range.mv=926;snapshot.range.raw=1200;snapshot.range.decoded=snapshot.range.selected=2;snapshot.range.initialized=true;
  snapshot.interval=3600000;snapshot.window=3600;snapshot.frames=8;snapshot.accepted=99;snapshot.quoteAge=20;
  snapshot.marketPrice=83300.12;snapshot.digits=83300;snapshot.connections=2;snapshot.bootConfirmed=true;
  snapshot.dataHeap=43000;snapshot.dataBlock=18000;snapshot.httpStack=4096;snapshot.cadenceTest=true;
  snapshot.httpKind=1;snapshot.httpSource=2;snapshot.httpStatus=200;snapshot.quoteRequested=true;
  snapshot.transitions=100;snapshot.priceChanges=100;snapshot.multiChanges=80;
  snapshot.axisLow=81000;snapshot.axisStep=2.5;
  snapshot.chartColumns=0x1FFFFF;snapshot.chartPixels=42;snapshot.historyCandles=6;snapshot.historySavedAt=1800000000;
  snapshot.ota.stage=ota::Stage::RetryLimited;snapshot.ota.httpStatus=200;snapshot.ota.checkedAt=55;
  snapshot.ota.total=123456;snapshot.ota.received=1234;snapshot.ota.retryAfter=86399;
  strcpy(snapshot.ota.candidate,"65535.65535.65535");
  telemetry::publish(snapshot);snapshot.accepted=101;telemetry::publish(snapshot);WiFi.connection=WL_CONNECTED;step();
  assert(listening&&telemetryWeb.running&&MDNS.running&&MDNS.port==8080);
  telemetryWeb.request("/telemetry",HTTP_GET);assert(telemetryWeb.statusCode==200&&!telemetryJson.overflowed());
  StaticJsonDocument<8192> response;assert(!deserializeJson(response,telemetryWeb.body.c_str()));
  assert(response["schema"]==1&&response["accepted_quotes"]==101&&response["nixie_frames"]==8);
  assert(response["dials"]["frequency"]["mv"]==309&&response["dials"]["frequency"]["decoded"]==-1&&response["dials"]["frequency"]["selected"]==4);
  assert(response["dials"]["range"]["pin"]==34&&response["refresh_ms"]==3600000&&response["boot_confirmed"]==true);
  assert(response["visible_output_verified"]==false&&response["dials"]["frequency"]["centers_mv"].size()==5);
  assert(response["ota"]["state"]=="retry-limited"&&response["ota"]["http_status"]==200&&response["ota"]["checked_at_ms"]==55);
  assert(response["ota"]["candidate_version"]=="65535.65535.65535"&&response["ota"]["total_bytes"]==123456);
  assert(response["ota"]["received_bytes"]==1234&&response["ota"]["retry_after_seconds"]==86399);
  assert(response["data_heap"]==43000&&response["data_largest_allocation"]==18000&&response["http_stack_remaining"]==4096);
  assert(response["cadence_test"]==true);
  assert(response["http_kind"]=="history"&&response["http_source"]=="Bitstamp"&&response["last_http_status"]==200);
  assert(response["quote_request_pending"]==true&&response["chart_columns_mask"]==0x1FFFFF&&response["chart_pixels"]==42);
  assert(response["price_transitions"]==100&&response["price_changes"]==100&&response["multi_digit_changes"]==80);
  assert(response["chart_type"]=="candles"&&response["chart_axis_low_usd"]==81000&&response["chart_usd_per_row"]==2.5);
  assert(response["history_candles"]==6&&response["history_saved_epoch"]==1800000000);
  assert(telemetryWeb.body.value.find("token")==std::string::npos&&telemetryWeb.body.value.find("password")==std::string::npos&&telemetryWeb.body.value.find("ssid")==std::string::npos);
  telemetryWeb.request("/",HTTP_GET);assert(telemetryWeb.statusCode==200&&telemetryWeb.body.value.find("Check for update now")!=std::string::npos);
  telemetryWeb.request("/control-token",HTTP_GET);assert(!deserializeJson(response,telemetryWeb.body.c_str()));std::string token=response["token"].as<const char*>();assert(token.size()==32);
  telemetryWeb.request("/control",HTTP_POST,{{"action","ota"}});assert(telemetryWeb.statusCode==403);
  telemetryWeb.request("/control",HTTP_POST,{{"action","ota"},{"token",token}},{{"Origin","https://evil.example"}});assert(telemetryWeb.statusCode==403);
  telemetryWeb.request("/control",HTTP_POST,{{"action","flash-unsigned"},{"token",token}});assert(telemetryWeb.statusCode==400);
  for(const char* action:{"ota","tubes","readings","chart"}){telemetryWeb.request("/control",HTTP_POST,{{"action",action},{"token",token}},{{"Origin","http://192.168.1.90:8080"}});assert(telemetryWeb.statusCode==202);}
  telemetryWeb.request("/control",HTTP_POST,{{"action","ota"},{"token",token}});assert(telemetryWeb.statusCode==503);
  telemetry::Command command;for(auto expected:{telemetry::Command::Ota,telemetry::Command::Tubes,telemetry::Command::Readings,telemetry::Command::Chart}){assert(telemetry::receive(command)&&command==expected);}assert(!telemetry::receive(command));
  telemetryWeb.request("/control",HTTP_POST,{{"action","cadence"},{"token",token}});assert(telemetryWeb.statusCode==202);
  assert(telemetry::receive(command)&&command==telemetry::Command::Cadence&&!telemetry::receive(command));
  snapshot.portal=true;telemetry::publish(snapshot);step();assert(!listening&&!MDNS.running);
  snapshot.portal=false;MDNS.fail=true;telemetry::publish(snapshot);step();assert(listening&&!mdns); // Direct IP still works if multicast fails.
  WiFi.connection=0;step();assert(!listening);
  snapshot.uptime=UINT32_MAX-100;telemetry::publish(snapshot);testMillis=25;WiFi.connection=WL_CONNECTED;step();
  telemetryWeb.request("/telemetry",HTTP_GET);assert(!deserializeJson(response,telemetryWeb.body.c_str())&&response["snapshot_age_ms"]==126);
  telemetryWeb.stop();vQueueDelete(snapshots);vQueueDelete(commands);snapshots=commands=nullptr;assert(!queues);
  std::cout<<"PASS: actual Wi-Fi telemetry handlers, latest snapshot ownership, all diagnostic fields, no credentials/tokens in samples, command authorization/queue bounds, OTA intent, network/portal gating, mDNS fallback and millis rollover\n";
}
