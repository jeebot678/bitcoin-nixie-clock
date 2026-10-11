#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <time.h>
#include "Arduino.h"
#include "WiFi.h"
#include "LivePrice.h"
#include "BoundedWebSocketTls.h"

struct Queue {std::vector<uint8_t> bytes;bool ready=false;explicit Queue(size_t size):bytes(size){}};
using QueueHandle_t=Queue*;
constexpr int pdTRUE=1,pdPASS=1;
bool failQueue=false,failTask=false;unsigned queues=0;
QueueHandle_t xQueueCreate(int count,size_t size){assert(count==1);if(failQueue)return nullptr;++queues;return new Queue(size);}
void vQueueDelete(QueueHandle_t queue){delete queue;--queues;}
int xQueueOverwrite(QueueHandle_t queue,const void* data){memcpy(queue->bytes.data(),data,queue->bytes.size());queue->ready=true;return pdTRUE;}
int xQueueReceive(QueueHandle_t queue,void* data,int){if(!queue->ready)return 0;memcpy(data,queue->bytes.data(),queue->bytes.size());queue->ready=false;return pdTRUE;}
int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned stack,void*,unsigned priority,void*,int core){assert(stack==16384&&priority==1&&core==0);return failTask?0:pdPASS;}
SerialMock Serial;EspMock ESP;WifiMock WiFi;uint32_t testMillis=100;
std::map<int,int>pins,modes,adc;std::vector<GpioEvent>gpioEvents;
const uint8_t caBundle[] asm("_binary_data_cert_x509_crt_bundle_bin_start")={0};
time_t testEpoch=0;
time_t testTime(time_t* output){if(output)*output=testEpoch;return testEpoch;}
#define time testTime
#include "../../src/LivePrice.cpp"
#undef time

std::string readFile(const std::string& path){std::ifstream file(path);assert(file.good());std::ostringstream out;out<<file.rdbuf();return out.str();}
int main(int argc,char** argv){
  assert(argc==2);std::string directory=argv[1];
  BoundedWebSocketTls bounded;assert(bounded.handshakeSeconds==6&&bounded.timeoutSeconds==3&&!bounded.verified);bounded.setCACertBundle(caBundle);assert(bounded.verified);
  uint32_t idleAt=testMillis;uint8_t byte;assert(bounded.read()<0&&testMillis==idleAt+1);
  assert(bounded.read(&byte,0)==0&&testMillis==idleAt+1);
  bounded.data={42,43};assert(bounded.read()==42&&bounded.read(&byte,1)==1&&byte==43&&testMillis==idleAt+1);
  failQueue=true;assert(!live::begin()&&!queues);failQueue=false;failTask=true;assert(!live::begin()&&!queues);failTask=false;assert(live::begin()&&queues==1);
  step();assert(!socket&&live::status().paused);WiFi.connection=WL_CONNECTED;step();assert(!socket); // No TLS before SNTP.
  testEpoch=btc::isoTimestamp("2026-10-11T05:34:22Z");
  assert(live::pauseAndWait(0));step();assert(!socket&&live::status().paused);live::resume();
  live::httpBusy(true);step();assert(!socket);live::httpBusy(false);
  {btc::TlsLease httpHandshake;assert(httpHandshake.acquire());step();assert(socket&&!live::status().connected&&socket->sent.empty());}
  step();assert(socket&&liveSockets()==1&&socket->ca==caBundle&&socket->protocol.empty()&&socket->reconnect==5000&&socket->ping==20000);
  assert(socket->host=="ws.kraken.com"&&socket->url=="/v2"&&socket->sent.size()==1&&socket->sent[0].find("bbo")!=std::string::npos);
  assert(live::status().connected&&!live::fresh(testMillis));
  auto kraken=readFile(directory+"/kraken_stream.json"),coinbase=readFile(directory+"/coinbase_stream.json");
  socket->emit(WStype_TEXT,kraken);assert(live::fresh(testMillis)&&live::status().quotes==1);
  live::Tick tick;assert(live::receive(tick)&&tick.source==1&&tick.quote.price==83006.3&&tick.quote.timestamp>0&&!live::receive(tick));
  socket->emit(WStype_TEXT,"{\"channel\":\"heartbeat\"}");socket->emit(WStype_TEXT,"{\"success\":true}");socket->emit(WStype_TEXT,std::string(4097,'x'));
  socket->emit(WStype_TEXT,"{\"channel\":\"ticker\",\"type\":\"update\",\"data\":[{\"symbol\":\"ETH/USD\",\"last\":4000,\"timestamp\":\"2026-10-11T05:34:22Z\"}]}");
  socket->emit(WStype_TEXT,"{\"channel\":\"ticker\",\"type\":\"update\",\"data\":[{\"symbol\":\"BTC/USD\",\"last\":80000,\"timestamp\":\"2025-10-11T05:34:22Z\"}]}");
  assert(live::status().quotes==1&&!live::receive(tick));testMillis+=1500;assert(!live::fresh(testMillis));
  socket->emit(WStype_TEXT,kraken);socket->emit(WStype_TEXT,kraken);assert(live::receive(tick)&&!live::receive(tick)&&live::status().quotes==3); // Bounded newest-value queue, not an unbounded backlog.
  socket->emit(WStype_DISCONNECTED,"HTTP 403");step();assert(!socket&&!live::status().connected&&cooldown[1].availableAt==testMillis+3600000);
  step();assert(socket&&source==0&&socket->host=="ws-feed.exchange.coinbase.com"&&liveSockets()==1);
  socket->emit(WStype_TEXT,coinbase);assert(live::receive(tick)&&tick.source==0&&tick.quote.price==83007);
  socket->emit(WStype_DISCONNECTED,"WebSocket handshake failed - HTTP 429");step();assert(!socket&&cooldown[0].availableAt==testMillis+60000);
  testMillis+=59999;step();assert(!socket);++testMillis;step();assert(socket&&source==0);
  assert(!live::pauseAndWait(0));step();assert(live::pauseAndWait(0)&&!socket&&!liveSockets());live::resume();step();assert(socket);
  WiFi.connection=0;step();assert(!socket&&live::status().paused);WiFi.connection=WL_CONNECTED;step();assert(socket);
  // Lost heartbeat triggers failover without blocking the main loop.
  testMillis+=35001;step();assert(!socket);assert(!live::status().connected);
  for(auto&state:cooldown){state.availableAt=testMillis;state.failures=0;}step();assert(socket);
  socket->emit(WStype_TEXT,"{\"success\":false,\"error\":\"subscription rejected\"}");step();assert(!socket);
  for(auto&state:cooldown){state.availableAt=testMillis;state.failures=0;}testMillis=UINT32_MAX-500;for(auto&state:cooldown)state.availableAt=testMillis;step();assert(socket);
  testMillis=501;socket->emit(WStype_TEXT,source?kraken:coinbase);assert(live::fresh(testMillis));
  closeSocket();vQueueDelete(ticks);ticks=nullptr;assert(!liveSockets()&&!queues);
  std::cout<<"PASS: real streaming worker; captured USD payloads, CA bundle, bounded queue, malformed/stale data, heartbeat/ACK handling, separate task, HTTP 403/429 backoff, provider failover, Wi-Fi/SNTP gating, OTA pause and rollover\n";
}
