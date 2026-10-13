#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>
#include <deque>
#include <time.h>
#include "Arduino.h"
#include "WiFi.h"
#include "LivePrice.h"
#include "BoundedWebSocketTls.h"
struct Queue {std::deque<std::vector<uint8_t>> values;size_t capacity,itemSize;Queue(size_t count,size_t size):capacity(count),itemSize(size){}};
using QueueHandle_t=Queue*;
constexpr int pdTRUE=1,pdPASS=1;
bool failQueue=false,failTask=false;unsigned queues=0;
QueueHandle_t xQueueCreate(int count,size_t size){assert(count==16);if(failQueue)return nullptr;++queues;return new Queue(count,size);}
void vQueueDelete(QueueHandle_t queue){delete queue;--queues;}
int xQueueSend(QueueHandle_t queue,const void* data,int){if(queue->values.size()==queue->capacity)return 0;queue->values.emplace_back(queue->itemSize);memcpy(queue->values.back().data(),data,queue->itemSize);return pdTRUE;}
int xQueueReceive(QueueHandle_t queue,void* data,int){if(queue->values.empty())return 0;memcpy(data,queue->values.front().data(),queue->itemSize);queue->values.pop_front();return pdTRUE;}
int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned stack,void* argument,unsigned priority,void*,int core){assert(stack==9216&&priority==0&&core==0&&argument);return failTask?0:pdPASS;}
SerialMock Serial;EspMock ESP;WifiMock WiFi;uint32_t testMillis=100;
std::map<int,int>pins,modes,adc;std::vector<GpioEvent>gpioEvents;
const uint8_t caBundle[] asm("_binary_data_cert_x509_crt_bundle_bin_start")={0};
time_t testEpoch=0;
time_t testTime(time_t* output){if(output)*output=testEpoch;return testEpoch;}
#define time testTime
#include "../../src/LivePrice.cpp"
#undef time
std::string readFile(const std::string& path){std::ifstream file(path);assert(file.good());std::ostringstream out;out<<file.rdbuf();return out.str();}
void steps(){for(auto& slot:slots)slot.step();}
int main(int argc,char** argv){
  assert(argc==2);std::string directory=argv[1];
  // TLS uses two separate record buffers. Fragmented RAM with no combined
  // 36 KiB block is usable; one large block alone does not prove both fit.
  testHeapBlocks()={20000,20000,28200};assert(network_memory::fits(8192));
  assert(testAllocations().empty()&&heap_caps_get_free_size(3)==68200);
  testHeapBlocks()={20000,10000,10000,28200};assert(!network_memory::fits(8192));
  assert(testAllocations().empty()&&heap_caps_get_free_size(3)==68200);testHeapBlocks().clear();
  BoundedWebSocketTls bounded;assert(bounded.handshakeSeconds==20&&bounded.timeoutSeconds==3&&!bounded.verified);bounded.setCACertBundle(caBundle);assert(bounded.verified);
  uint32_t idleAt=testMillis;uint8_t byte;assert(bounded.read()<0&&testMillis==idleAt+1);
  assert(bounded.read(&byte,0)==0&&testMillis==idleAt+1);bounded.data={42,43};assert(bounded.read()==42&&bounded.read(&byte,1)==1&&byte==43);
  failQueue=true;assert(!live::begin()&&!queues);failQueue=false;failTask=true;assert(!live::begin()&&!queues);failTask=false;assert(live::begin()&&queues==2);
  steps();assert(!liveSockets()&&live::status().paused);WiFi.connection=WL_CONNECTED;steps();assert(!liveSockets());
  testEpoch=btc::isoTimestamp("2026-10-11T05:34:22Z");assert(live::pauseAndWait(0));steps();assert(!liveSockets());live::resume();
  live::httpBusy(true);steps();assert(!liveSockets());live::httpBusy(false);
  {btc::TlsLease httpHandshake;assert(httpHandshake.acquire());steps();assert(liveSockets()==2&&!live::status().connected&&slots[0].socket->sent.empty());}
  // Both sockets passed the initial check while another task held TLS. The
  // first connection then consumes data RAM; the second must recheck under
  // the gate instead of attempting SSL with its now-obsolete budget.
  connectedHook()=[](){testDataHeap()=43000;testDataBlock()=18000;};
  steps();assert(live::status().connections==1&&!slots[1].handshake);
  connectedHook()={};testDataHeap()=testDataBlock()=UINT32_MAX;
  steps();assert(live::status().connections==2&&!live::fresh(testMillis));
  auto& primary=slots[0];auto& secondary=slots[1];assert(primary.source==0&&secondary.source==1);
  assert(primary.socket->host=="ws-feed.exchange.coinbase.com"&&secondary.socket->host=="ws.kraken.com"&&secondary.socket->url=="/v2"&&secondary.socket->sent[0].find("bbo")!=std::string::npos&&secondary.socket->sent[1].find("trade")!=std::string::npos);
  for(auto& slot:slots)assert(slot.socket->ca==caBundle&&slot.socket->protocol.empty()&&slot.socket->reconnect==5000&&slot.socket->ping==20000);
  auto kraken=readFile(directory+"/kraken_stream.json"),coinbase=readFile(directory+"/coinbase_stream.json");
  primary.socket->emit(WStype_TEXT,coinbase);
  for(unsigned i=0;i<100;++i)secondary.socket->emit(WStype_TEXT,kraken);
  live::Tick tick;std::set<unsigned> seen;
  while(live::receive(tick)){seen.insert(tick.source);assert(tick.quote.price>83000&&tick.quote.timestamp>0);}
  assert(seen.size()==2&&live::status().quotes==101&&live::fresh(testMillis)); // A busy feed cannot erase the other slot.
  assert(live::status().dropped==84&&live::status().providers==3);
  assert(live::status().quoteCounts[0]==1&&live::status().quoteCounts[1]==100);
  // Every real price in a matched-trade batch reaches the main loop, rather
  // than overwriting one latest-value cell and losing candle highs/lows.
  const std::string batch="{\"channel\":\"trade\",\"type\":\"update\",\"data\":["
    "{\"symbol\":\"BTC/USD\",\"price\":83009.1,\"trade_id\":1,\"timestamp\":\"2026-10-11T05:34:21.100Z\"},"
    "{\"symbol\":\"BTC/USD\",\"price\":83015.5,\"trade_id\":2,\"timestamp\":\"2026-10-11T05:34:21.200Z\"},"
    "{\"symbol\":\"BTC/USD\",\"price\":83002.7,\"trade_id\":3,\"timestamp\":\"2026-10-11T05:34:21.300Z\"}]}";
  secondary.socket->emit(WStype_TEXT,batch);
  for(double price:{83009.1,83015.5,83002.7})assert(live::receive(tick)&&tick.source==1&&std::abs(tick.quote.price-price)<1e-6&&tick.quote.timestampMs>0&&!tick.fx);
  secondary.socket->emit(WStype_TEXT,kraken);assert(live::receive(tick)); // Ticker messages cannot reset the trade ID fence.
  secondary.socket->emit(WStype_TEXT,batch);assert(!live::receive(tick));
  std::string exchangeFx="{\"channel\":\"ticker\",\"type\":\"update\",\"data\":[{\"symbol\":\"USDT/USD\",\"last\":0.99883,\"timestamp\":\"2026-10-11T05:34:22Z\"}]}";
  unsigned pricesBefore=live::status().quotes;
  secondary.socket->emit(WStype_TEXT,exchangeFx);assert(live::receive(tick)&&tick.source==1&&std::abs(tick.fx-0.99883)<1e-9&&!tick.quote.price);
  assert(live::status().quotes==pricesBefore); // FX updates are never counted as Bitcoin prices.
  primary.socket->emit(WStype_TEXT,"{\"channel\":\"heartbeat\"}");primary.socket->emit(WStype_TEXT,std::string(8193,'x'));
  primary.socket->emit(WStype_TEXT,"{\"channel\":\"ticker\",\"type\":\"update\",\"data\":[{\"symbol\":\"ETH/USD\",\"last\":4000,\"timestamp\":\"2026-10-11T05:34:22Z\"}]}");
  assert(!live::receive(tick));testMillis+=5000;assert(!live::fresh(testMillis));
  secondary.socket->emit(WStype_TEXT,exchangeFx);assert(live::receive(tick)&&tick.fx>0.5&&!live::fresh(testMillis));
  // Dispatch alone must not tear down either established subscription.
  live::httpBusy(true);steps();assert(primary.connected&&secondary.connected&&!secondaryReserved);
  assert(live::prepareHttp(8192,0));primary.socket->emit(WStype_TEXT,coinbase);assert(live::receive(tick)&&tick.source==0);
  // Reported on the real ESP32: ~89 KiB total / 45 KiB largest included IRAM,
  // while the byte-accessible pool could not allocate another TLS session.
  ESP.freeHeap=89000;ESP.largest=45044;testDataHeap()=43000;testDataBlock()=18000;
  assert(!live::prepareHttp(8192,0,false,false));assert(primary.connected&&!primaryReserved&&secondaryReserved);
  steps();assert(primary.connected&&secondary.paused&&!secondary.socket);
  assert(!live::prepareHttp(8192,0,false,false)&&primary.connected&&!primaryReserved);
  assert(!live::prepareHttp(8192,0));steps();assert(!liveSockets()&&primary.paused);
  testDataHeap()=testDataBlock()=UINT32_MAX;ESP.largest=100000;
  ESP.freeHeap=150000;assert(live::prepareHttp(8192,0));live::httpBusy(false);steps();assert(live::status().connections==2);
  primary.socket->emit(WStype_DISCONNECTED,"HTTP 403");primary.step();assert(!primary.socket&&primary.cooldown[0].availableAt==testMillis+3600000);
  primary.step();assert(primary.source==2&&primary.socket->host=="ws.bitstamp.net"); // Kraken belongs to the other worker; USDT is ineligible without FX.
  std::string stamp="{\"event\":\"trade\",\"channel\":\"live_trades_btcusd\",\"data\":{\"price\":83008,\"id\":42,\"microtimestamp\":\""+std::to_string(uint64_t(testEpoch)*1000000)+"\"}}";
  primary.socket->emit(WStype_TEXT,stamp);assert(live::receive(tick)&&tick.source==2&&tick.quote.price==83008);
  primary.socket->emit(WStype_TEXT,stamp);assert(!live::receive(tick));
  primary.socket->emit(WStype_TEXT,"{\"event\":\"bts:request_reconnect\"}");primary.step();assert(!primary.socket);primary.step();assert(primary.source==4);
  std::string trade="[77,\"te\",[42,"+std::to_string(uint64_t(testEpoch)*1000)+",0.01,83009]]";
  primary.socket->emit(WStype_TEXT,trade);assert(!live::receive(tick)); // No trusted symbol/channel binding yet.
  primary.socket->emit(WStype_TEXT,"{\"event\":\"subscribed\",\"channel\":\"trades\",\"symbol\":\"tETHUSD\",\"chanId\":77}");
  primary.socket->emit(WStype_TEXT,trade);assert(!live::receive(tick));
  primary.socket->emit(WStype_TEXT,"{\"event\":\"subscribed\",\"channel\":\"trades\",\"symbol\":\"tBTCUSD\",\"chanId\":77}");
  primary.socket->emit(WStype_TEXT,trade);assert(live::receive(tick)&&tick.source==4&&tick.quote.price==83009);
  primary.socket->emit(WStype_TEXT,trade);primary.socket->emit(WStype_TEXT,"[77,\"tu\",[43,1791696862000,0.01,83010]]");assert(!live::receive(tick));
  primary.socket->emit(WStype_DISCONNECTED,"HTTP 429");primary.step();assert(primary.cooldown[4].availableAt==testMillis+60000);
  primary.step();assert(primary.source==3&&primary.socket->host=="ws.gemini.com");
  std::string gemini="{\"E\":"+std::to_string(uint64_t(testEpoch)*1000000000+987654321)+",\"s\":\"btcusd\",\"t\":2840141032956701,\"p\":\"83017.89\",\"q\":\"0.01\",\"m\":true}";
  primary.socket->emit(WStype_TEXT,gemini);assert(live::receive(tick)&&tick.source==3&&tick.quote.timestampMs==uint64_t(testEpoch)*1000+987);
  primary.socket->emit(WStype_TEXT,gemini);assert(!live::receive(tick));
  primary.socket->emit(WStype_TEXT,"{\"id\":\"1\",\"status\":429,\"error\":{\"code\":-1003,\"msg\":\"Rate limit exceeded\"}}");
  primary.step();assert(primary.cooldown[3].availableAt==testMillis+60000&&!primary.socket);
  primary.nextProvider=2;live::allowUsdt(true);primary.step();
  assert(primary.source==5&&primary.socket->host=="data-stream.binance.vision"&&primary.socket->url=="/ws/btcusdt@aggTrade"&&primary.socket->sent.empty());
  std::string binance="{\"e\":\"aggTrade\",\"s\":\"BTCUSDT\",\"p\":\"83016.54\",\"a\":152345678,\"T\":"+std::to_string(uint64_t(testEpoch)*1000+123)+"}";
  primary.socket->emit(WStype_TEXT,binance);assert(live::receive(tick)&&tick.source==5&&std::abs(tick.quote.price-83016.54)<1e-6);
  primary.socket->emit(WStype_TEXT,binance);assert(!live::receive(tick));
  live::allowUsdt(false);primary.step();assert(!primary.socket||primary.source!=5); // Expired conversion closes an unusable stream and leaves USD failover eligible.
  assert(!live::pauseAndWait(0));steps();assert(live::pauseAndWait(0)&&!liveSockets());live::resume();
  WiFi.connection=0;steps();assert(live::status().paused&&!liveSockets());WiFi.connection=WL_CONNECTED;
  testMillis=UINT32_MAX-500;for(auto& slot:slots){slot.nextProvider=slot.index;for(auto& state:slot.cooldown)state.availableAt=testMillis;}
  steps();testMillis=501;for(auto& slot:slots)if(slot.source==1)slot.socket->emit(WStype_TEXT,kraken);
  assert(live::fresh(testMillis));
  testMillis+=35001;steps();assert(!live::status().connected);
  // The pinned library begins with lastPing=0 and pongReceived=false. A
  // connected feed must not time out before its first scheduled ping, nor
  // lose a healthy data stream merely because control PONGs are delayed.
  for(auto& slot:slots){slot.close();slot.nextProvider=slot.index;for(auto& state:slot.cooldown)state.availableAt=testMillis;}
  steps();uint32_t heartbeatStart=testMillis;
  for(auto& slot:slots)assert(slot.socket&&slot.connected&&slot.socket->misses==0&&slot.socket->pong==10000);
  testMillis=heartbeatStart+11001;steps();assert(live::status().connections==2);
  for(unsigned i=0;i<9;++i){
    testMillis+=5000;
    for(auto& slot:slots)slot.socket->emit(WStype_TEXT,slot.source==0?coinbase:kraken);
    steps();assert(live::status().connections==2);
  }
  testMillis+=35001;steps();assert(!live::status().connected);
  assert(live::status().slots[0].reason==live::Reason::NoMessages);
  // Silent TLS connect failures expose the exact negative error before the
  // next library iteration can delete the SSL object. They release the gate
  // immediately, keeping the other worker free to establish its connection.
  for(auto& slot:slots){slot.close();slot.nextProvider=slot.index;for(auto& state:slot.cooldown)state.availableAt=testMillis;}
  {btc::TlsLease held;assert(held.acquire());steps();}
  slots[0].socket->tlsFailure(-32512);slots[0].step();
  auto diagnosis=live::status();
  assert(!slots[0].socket&&!slots[0].handshake&&diagnosis.slots[0].reason==live::Reason::Tls&&diagnosis.slots[0].reasonSource==0&&diagnosis.slots[0].tlsError==-32512&&diagnosis.failures[0]>0);
  slots[1].step();assert(slots[1].connected);
  slots[0].step();assert(slots[0].source!=0&&live::status().slots[0].reasonSource==0); // Failure identity survives provider changes.
  // A sparse fallback reclaims an eligible preferred feed after 30 seconds,
  // while another connection keeps delivering prices. A reserved upgrade
  // prevents both workers from dropping their fallbacks together.
  for(auto& slot:slots){slot.close();for(auto& state:slot.cooldown)state.availableAt=testMillis;}
  slots[0].nextProvider=3;slots[1].nextProvider=4;live::allowUsdt(false);steps();
  assert(slots[0].source==2&&slots[1].source==4);
  uint32_t fallbackStart=testMillis;testMillis=fallbackStart+30001;
  {btc::TlsLease held;assert(held.acquire());slots[0].step();
    assert(!slots[0].connected&&slots[0].source==0&&slots[1].connected&&recoveryOwner==1);
    slots[1].step();assert(slots[1].connected&&slots[1].source==4&&recoveryOwner==1);
  }
  slots[0].step();assert(slots[0].connected&&slots[0].source==0&&!recoveryOwner);
  assert(live::status().slots[0].reason==live::Reason::PreferredRecovery&&live::status().slots[0].reasonSource==2);
  slots[1].step();assert(slots[1].source==1&&slots[1].connected); // Peer preferred ownership is respected.
  // With only one live fallback, retain it while the idle worker recovers.
  slots[0].close();slots[1].close();slots[0].nextProvider=3;slots[0].step();
  assert(slots[0].connected&&slots[0].source==2);testMillis+=30001;slots[0].step();assert(slots[0].connected&&slots[0].source==2);
  // Independently captured public trade payloads validate schema/timestamps.
  for(uint8_t provider:{uint8_t(2),uint8_t(4)}){
    StaticJsonDocument<2048> captured;assert(!deserializeJson(captured,readFile(directory+(provider==2?"/bitstamp_stream.json":"/bitfinex_stream.json"))));
    uint32_t capturedChannel=provider==4?captured[0].as<uint32_t>():0;
    double timestamp=0;assert(btc::number(provider==2?captured["data"]["microtimestamp"].as<JsonVariantConst>():captured[2][1].as<JsonVariantConst>(),timestamp));
    uint32_t epoch=uint32_t(timestamp/(provider==2?1000000:1000));btc::Quote quote;
    assert(btc::parseStreamQuote(provider,captured.as<JsonVariantConst>(),epoch,quote,capturedChannel));
    assert(!btc::parseStreamQuote(provider,captured.as<JsonVariantConst>(),epoch+121,quote,capturedChannel));
    if(provider==4)assert(!btc::parseStreamQuote(provider,captured.as<JsonVariantConst>(),epoch,quote,capturedChannel+1));
  }
  // New fallback schemas require the exact BTC instrument and execution
  // timestamp; nanosecond Gemini times retain their integer precision.
  for(uint8_t provider:{uint8_t(3),uint8_t(5)}){
    StaticJsonDocument<1024> captured;const char* name=provider==3?"gemini":"binance";
    assert(!deserializeJson(captured,readFile(directory+"/"+name+"_stream.json")));
    uint64_t stamp=captured[provider==3?"E":"T"].as<uint64_t>();
    uint32_t epoch=uint32_t(stamp/(provider==3?1000000000ULL:1000ULL));btc::Quote quote;
    assert(btc::parseStreamQuote(provider,captured.as<JsonVariantConst>(),epoch,quote));
    assert(quote.timestampMs==stamp/(provider==3?1000000ULL:1ULL));
    assert(!btc::parseStreamQuote(provider,captured.as<JsonVariantConst>(),epoch+121,quote));
    captured["s"]=provider==3?"ethusd":"ETHUSDT";
    assert(!btc::parseStreamQuote(provider,captured.as<JsonVariantConst>(),epoch,quote));
    captured["s"]=provider==3?"btcusd":"BTCUSDT";captured[provider==3?"t":"a"]=-1;
    assert(!btc::parseStreamQuote(provider,captured.as<JsonVariantConst>(),epoch,quote));
  }
  for(auto& slot:slots){slot.close();vQueueDelete(slot.ticks);slot.ticks=nullptr;}assert(!queues&&!liveSockets());
  std::cout<<"PASS: dual live workers, bounded burst queues, USD/USDT parsers, channel binding/deduplication, HTTP memory reservation, uninterrupted primary, failover/backoff, Wi-Fi/SNTP, OTA pause and rollover\n";
}
