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
void bookTests(const std::string& directory){
  btc::BitfinexBook book;btc::Quote quote;StaticJsonDocument<2048> message;
  auto load=[&](const std::string& value)->JsonVariantConst{message.clear();assert(!deserializeJson(message,value));return message.as<JsonVariantConst>();};
  assert(!book.configure(load("{\"event\":\"conf\",\"status\":\"OK\",\"flags\":0}")));
  assert(book.configure(load("{\"event\":\"conf\",\"status\":\"OK\",\"flags\":32768}")));
  const std::string ack="{\"event\":\"subscribed\",\"channel\":\"book\",\"symbol\":\"tBTCUSD\",\"prec\":\"P0\",\"freq\":\"F0\",\"len\":\"1\",\"chanId\":77}";
  for(const char* field:{"symbol","prec","freq","len"}){
    load(ack);message[field]="wrong";assert(!book.bind(message.as<JsonVariantConst>())&&!book.channel);
  }
  assert(book.bind(load(ack))&&book.channel==77&&!book.ready);
  uint32_t epoch=1791917933;uint64_t stamp=uint64_t(epoch)*1000;
  auto packet=[&](const std::string& value,uint64_t time)->JsonVariantConst{return load("[77,"+value+","+std::to_string(time)+"]");};
  assert(book.apply(packet("[[83000,1,0.1],[83020,1,-0.1]]",stamp),epoch,quote)&&quote.price==83010&&quote.kind==btc::QuoteKind::BboMidpoint&&quote.timestampMs==stamp);
  assert(!book.apply(packet("[83000,0,1]",stamp+1),epoch,quote)&&!book.bid&&!book.corrupted);
  assert(book.apply(packet("[83010,1,0.3]",stamp+2),epoch,quote)&&quote.price==83015);
  assert(book.apply(packet("[83000,0,1]",stamp+3),epoch,quote)&&quote.price==83015); // Removing an old replaced price cannot erase the current best.
  assert(!book.apply(packet("[83040,1,0.1]",stamp+4),epoch,quote)&&!book.corrupted); // Crossed transitional state cannot emit a midpoint.
  assert(book.apply(packet("[83050,1,-0.1]",stamp+5),epoch,quote)&&quote.price==83045);
  assert(!book.apply(packet("[85000,1,-0.1]",stamp+6),epoch,quote)&&!book.corrupted); // Excessive spread never emits.
  assert(!book.apply(packet("[83060,1,-0.1]",stamp+5),epoch,quote)&&book.corrupted); // Older publications cannot mutate current state.
  book.reset();assert(!book.timestamps&&!book.ready&&!book.channel);
  for(const std::string& invalid:{"[[83000,1,0.1],[83001,1,0.2]]","[[83000,1,0.1]]","[83000,-1,0.1]","[83000,1.5,0.1]","[83000,0,0.5]"}){
    book.reset();assert(book.configure(load("{\"event\":\"conf\",\"status\":\"OK\",\"flags\":32768}"))&&book.bind(load(ack)));
    if(invalid[1]!='[')assert(book.apply(packet("[[83000,1,0.1],[83020,1,-0.1]]",stamp),epoch,quote));
    assert(!book.apply(packet(invalid,stamp+1),epoch,quote)&&book.corrupted);
  }
  for(uint64_t invalidTime:{stamp-6000,stamp+11000,uint64_t(UINT64_MAX)}){
    book.reset();assert(book.configure(load("{\"event\":\"conf\",\"status\":\"OK\",\"flags\":32768}"))&&book.bind(load(ack)));
    assert(!book.apply(packet("[[83000,1,0.1],[83020,1,-0.1]]",invalidTime),epoch,quote)&&book.corrupted);
  }
  // Independently captured sequence reproduces the depth-one reference,
  // including old-level removals, quantity changes and channel heartbeats.
  DynamicJsonDocument captured(65536);assert(!deserializeJson(captured,readFile(directory+"/bitfinex_book_session.json")));
  book.reset();double bid=0,ask=0;unsigned emitted=0;
  for(JsonVariantConst root:captured.as<JsonArrayConst>()){
    if(root.is<JsonObjectConst>()){
      if(btc::equals(root["event"],"conf"))assert(book.configure(root));
      else assert(book.bind(root));continue;
    }
    if(btc::equals(root[1],"hb")){assert(!book.apply(root,uint32_t(root[2].as<uint64_t>()/1000),quote)&&!book.corrupted);continue;}
    auto reference=[&](JsonVariantConst row){double p=row[0].as<double>(),amount=row[2].as<double>();double& side=amount>0?bid:ask;if(row[1].as<unsigned>())side=p;else if(side==p)side=0;};
    if(root[1][0].is<JsonArrayConst>()){bid=ask=0;for(JsonVariantConst row:root[1].as<JsonArrayConst>())reference(row);}else reference(root[1]);
    bool valid=bid>0&&ask>=bid&&(ask-bid)/((ask+bid)/2)<=.01;
    assert(book.apply(root,uint32_t(root[2].as<uint64_t>()/1000),quote)==valid&&!book.corrupted);
    if(valid){assert(quote.price==(bid+ask)/2&&quote.kind==btc::QuoteKind::BboMidpoint);++emitted;}
  }
  assert(emitted==71);
}
void currentBookStatusTests(){
  for(auto& slot:slots){slot.close();live::Tick ignored;while(xQueueReceive(slot.ticks,&ignored,0)==pdTRUE){}}
  auto& slot=slots[1];live::Tick tick;
  const std::string conf="{\"event\":\"conf\",\"status\":\"OK\",\"flags\":32768}";
  const std::string ack="{\"event\":\"subscribed\",\"channel\":\"book\",\"symbol\":\"tBTCUSD\",\"prec\":\"P0\",\"freq\":\"F0\",\"len\":\"1\",\"chanId\":77}";
  const uint64_t stamp=uint64_t(testEpoch)*1000;
  auto emit=[&](const std::string& value){slot.socket->emit(WStype_TEXT,value);};
  auto packet=[&](const std::string& value,uint64_t published){emit("[77,"+value+","+std::to_string(published)+"]");};
  auto open=[&](){
    slot.close();while(live::receive(tick)){}
    slot.nextProvider=1;for(auto& state:slot.cooldown)state.availableAt=testMillis;
    slot.step();assert(slot.connected&&slot.source==4&&!live::status().currentBooks);
    emit(conf);emit(ack);assert(!live::status().currentBooks);
  };
  auto snapshot=[&](){packet("[[83000,1,0.1],[83020,1,-0.1]]",stamp);assert(live::receive(tick)&&tick.quote.kind==btc::QuoteKind::BboMidpoint&&live::status().currentBooks==16);};
  open();packet("\"hb\"",stamp);assert(!live::status().currentBooks&&!live::receive(tick));snapshot();
  uint32_t received=slot.receivedAt,quotes=slot.quotes,sourceQuotes=slot.sourceQuotes[4];
  uint64_t published=slot.book.publishedAt;
  // An unchanged, subscribed order book remains current across the 120 s
  // no-trade deadline. Heartbeats maintain transport liveness, never prices.
  for(unsigned i=0;i<9;++i){
    testMillis+=15000;packet("\"hb\"",stamp);slot.step();
    assert(slot.connected&&live::status().currentBooks==16&&live::fresh(testMillis)&&!live::receive(tick));
    assert(slot.receivedAt==received&&slot.quotes==quotes&&slot.sourceQuotes[4]==sourceQuotes&&slot.book.publishedAt==published);
  }
  assert(live::status().quoteAges[4]>=135000);
  testMillis+=35001;assert(!live::status().currentBooks&&!live::fresh(testMillis));
  slot.step();assert(!slot.connected&&live::status().slots[1].reason==live::Reason::NoMessages);
  open();snapshot();
  packet("[83000,0,1]",stamp+1);assert(!live::status().currentBooks&&!live::receive(tick));
  testMillis+=6000;packet("\"hb\"",stamp+1);assert(!live::status().currentBooks&&!live::fresh(testMillis));
  packet("[83010,1,0.1]",stamp+2);assert(live::receive(tick)&&live::status().currentBooks==16);
  packet("[83040,1,0.1]",stamp+3);assert(!live::status().currentBooks&&!live::receive(tick));
  packet("[83050,1,-0.1]",stamp+4);assert(live::receive(tick)&&live::status().currentBooks==16);
  packet("[85000,1,-0.1]",stamp+5);assert(!live::status().currentBooks&&!live::receive(tick));
  packet("[83050,1,-0.1]",stamp+6);assert(live::receive(tick)&&live::status().currentBooks==16);
  emit(ack);assert(!live::status().currentBooks);packet("\"hb\"",stamp+6);assert(!live::status().currentBooks);
  snapshot();emit("{broken");assert(!live::status().currentBooks);slot.step();assert(!slot.connected&&live::status().slots[1].reason==live::Reason::Protocol);
  open();snapshot();packet("[83010,1,0.1]",stamp-6000);assert(!live::status().currentBooks&&!live::receive(tick));slot.step();assert(!slot.connected);
  open();snapshot();emit("{\"event\":\"unsubscribed\",\"chanId\":77}");assert(!live::status().currentBooks);slot.step();assert(!slot.connected&&live::status().slots[1].reason==live::Reason::SubscriptionError);
  open();snapshot();slot.socket->emit(WStype_DISCONNECTED,"connection lost");assert(!live::status().currentBooks);slot.close();
}
void convertedBookStatusTests(){
  for(auto& slot:slots){slot.close();live::Tick ignored;while(xQueueReceive(slot.ticks,&ignored,0)==pdTRUE){}}
  auto& slot=slots[1];live::Tick tick;uint64_t stamp=uint64_t(testEpoch)*1000;
  auto emit=[&](const std::string& value){slot.socket->emit(WStype_TEXT,value);};
  auto ack=[&](const char* symbol,unsigned channel){emit("{\"event\":\"subscribed\",\"channel\":\"book\",\"symbol\":\""+std::string(symbol)+"\",\"prec\":\"P0\",\"freq\":\"F0\",\"len\":\"1\",\"chanId\":"+std::to_string(channel)+"}");};
  auto packet=[&](unsigned channel,const std::string& data,uint64_t published){emit("["+std::to_string(channel)+","+data+","+std::to_string(published)+"]");};
  auto open=[&](){
    slot.close();while(live::receive(tick)){}
    slot.nextProvider=1;for(auto& state:slot.cooldown)state.availableAt=testMillis;slot.step();
    assert(slot.source==4&&slot.socket->sent.size()==4&&slot.socket->sent[2].find("tBTCUST")!=std::string::npos&&slot.socket->sent[3].find("tUSTUSD")!=std::string::npos);
    emit("{\"event\":\"conf\",\"status\":\"OK\",\"flags\":32768}");ack("tBTCUSD",77);ack("tBTCUST",78);ack("tUSTUSD",79);
    assert(!live::status().currentBooks&&!live::status().currentConvertedBooks);
  };
  auto complete=[&](){
    packet(77,"[[83050,1,0.1],[83060,1,-0.1]]",stamp);assert(live::receive(tick)&&tick.quote.kind==btc::QuoteKind::BboMidpoint);
    packet(78,"[[83200,1,0.1],[83220,1,-0.1]]",stamp+3);assert(!live::receive(tick)&&!live::status().currentConvertedBooks);
    packet(79,"[[1,1,100],[1.0001,1,-100]]",stamp+5);assert(live::receive(tick)&&tick.source==4&&tick.quote.kind==btc::QuoteKind::ConvertedBboMidpoint&&std::abs(tick.quote.price-83210*1.00005)<1e-6&&tick.quote.timestampMs==stamp+5&&tick.quote.timestamp==testEpoch);
    assert(live::status().currentBooks==16&&live::status().currentConvertedBooks==16);
  };
  open();complete();uint32_t received=slot.receivedAt,quotes=slot.quotes;
  for(unsigned i=0;i<9;++i){
    testMillis+=15000;for(unsigned channel:{77U,78U,79U})packet(channel,"\"hb\"",stamp+5);slot.step();
    assert(live::status().currentBooks==16&&live::status().currentConvertedBooks==16&&live::fresh(testMillis));
    assert(slot.receivedAt==received&&slot.quotes==quotes&&!live::receive(tick)&&slot.usdtBook.publishedAt==stamp+3&&slot.fxBook.publishedAt==stamp+5);
  }
  // Socket/PONG and other channels cannot hide a dead conversion leg.
  for(unsigned i=0;i<3;++i){testMillis+=12000;packet(77,"\"hb\"",stamp+5);packet(78,"\"hb\"",stamp+5);slot.socket->emit(WStype_PONG,"");slot.step();}
  assert(slot.connected&&live::status().currentBooks==16&&!live::status().currentConvertedBooks);
  packet(78,"[83210,1,0.1]",stamp+6);assert(!live::receive(tick));
  packet(79,"\"hb\"",stamp+5);assert(live::status().currentConvertedBooks==16&&slot.receivedAt==received&&slot.quotes==quotes&&!live::receive(tick));
  packet(79,"[1,0,1]",stamp+7);assert(!live::status().currentConvertedBooks&&live::status().currentBooks==16&&!live::receive(tick));
  packet(79,"\"hb\"",stamp+7);assert(!live::status().currentConvertedBooks);
  packet(79,"[1,1,100]",stamp+8);assert(live::receive(tick)&&tick.quote.kind==btc::QuoteKind::ConvertedBboMidpoint&&tick.quote.timestampMs==stamp+8);
  packet(79,"[0.9999,1,-100]",stamp+9);assert(!live::status().currentConvertedBooks&&!live::receive(tick));
  packet(79,"[1.0001,1,-100]",stamp+10);assert(live::receive(tick)&&live::status().currentConvertedBooks==16);
  packet(79,"[[0.1,1,100],[0.10001,1,-100]]",stamp+11);assert(!live::status().currentConvertedBooks&&!live::receive(tick));
  packet(79,"[[1,1,100],[1.0001,1,-100]]",stamp+12);assert(live::receive(tick)&&live::status().currentConvertedBooks==16);
  ack("tBTCUST",78);assert(!live::status().currentConvertedBooks&&live::status().currentBooks==16);packet(78,"\"hb\"",stamp+12);assert(!live::status().currentConvertedBooks);
  packet(78,"[[83200,1,0.1],[83220,1,-0.1]]",stamp+13);assert(live::receive(tick)&&tick.quote.timestampMs==stamp+13);
  packet(79,"[1,1]",stamp+14);assert(!live::status().currentBooks&&!live::status().currentConvertedBooks);slot.step();assert(!slot.connected);
  open();complete();packet(79,"[1,1,100]",stamp-6000);assert(!live::status().currentConvertedBooks&&!live::status().currentBooks&&!live::receive(tick));slot.step();assert(!slot.connected);
  open();ack("tUSTUSD",78);assert(slot.failed&&!live::status().currentConvertedBooks);slot.close();
}
int main(int argc,char** argv){
  assert(argc==2);std::string directory=argv[1];
  bookTests(directory);
  // TLS uses two separate record buffers. Fragmented RAM with no combined
  // 36 KiB block is usable; one large block alone does not prove both fit.
  testHeapBlocks()={20000,20000,28200};assert(network_memory::fits(8192));
  assert(testAllocations().empty()&&heap_caps_get_free_size(3)==68200);
  testHeapBlocks()={20000,10000,10000,28200};assert(!network_memory::fits(8192));
  assert(testAllocations().empty()&&heap_caps_get_free_size(3)==68200);testHeapBlocks().clear();
  BoundedWebSocketTls bounded;assert(bounded.handshakeSeconds==20&&bounded.timeoutSeconds==3&&!bounded.verified);bounded.setCACertBundle(caBundle);assert(bounded.verified);
  uint32_t idleAt=testMillis;uint8_t byte;assert(bounded.read()<0&&testMillis==idleAt+1);
  assert(bounded.read(&byte,0)==0&&testMillis==idleAt+1);bounded.data={42,43};assert(bounded.read()==42&&bounded.read(&byte,1)==1&&byte==43);
  failQueue=true;assert(!live::begin()&&!queues);failQueue=false;failTask=true;assert(!live::begin()&&!queues);failTask=false;assert(live::begin()&&queues==2);assert(providers[0]==0&&providers[1]==4);slots[1].nextProvider=2; // Exercise Kraken fallback while checking preferred IDs.
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
  primary.cooldown[4].availableAt=testMillis+5000;primary.socket->emit(WStype_DISCONNECTED,"HTTP 403");primary.step();assert(!primary.socket&&primary.cooldown[0].availableAt==testMillis+3600000);
  primary.step();assert(primary.source==2&&primary.socket->host=="ws.bitstamp.net"); // Kraken belongs to the other worker; USDT is ineligible without FX.
  std::string stamp="{\"event\":\"trade\",\"channel\":\"live_trades_btcusd\",\"data\":{\"price\":83008,\"id\":42,\"microtimestamp\":\""+std::to_string(uint64_t(testEpoch)*1000000)+"\"}}";
  primary.socket->emit(WStype_TEXT,stamp);assert(live::receive(tick)&&tick.source==2&&tick.quote.price==83008);
  primary.socket->emit(WStype_TEXT,stamp);assert(!live::receive(tick));
  primary.socket->emit(WStype_TEXT,"{\"event\":\"bts:request_reconnect\"}");primary.step();assert(!primary.socket);primary.nextProvider=1;primary.cooldown[4].availableAt=testMillis;primary.step();assert(primary.source==4);
  std::string bookSnapshot="[77,[[83000,1,0.1],[83020,1,-0.1]],"+std::to_string(uint64_t(testEpoch)*1000)+"]";
  primary.socket->emit(WStype_TEXT,bookSnapshot);assert(!live::receive(tick)); // No bound channel/config yet.
  primary.socket->emit(WStype_TEXT,"{\"event\":\"conf\",\"status\":\"OK\",\"flags\":32768}");
  primary.socket->emit(WStype_TEXT,"{\"event\":\"subscribed\",\"channel\":\"book\",\"symbol\":\"tBTCUSD\",\"prec\":\"P0\",\"freq\":\"F0\",\"len\":\"1\",\"chanId\":77}");
  primary.socket->emit(WStype_TEXT,bookSnapshot);assert(live::receive(tick)&&tick.source==4&&tick.quote.price==83010&&tick.quote.kind==btc::QuoteKind::BboMidpoint);
  assert(primary.socket->sent[0].find("32768")!=std::string::npos&&primary.socket->sent[1].find("book")!=std::string::npos);
  primary.socket->emit(WStype_TEXT,"[77,\"hb\",1791696862000]");
  primary.socket->emit(WStype_TEXT,"[77,\"te\",[42,1791696862000,0.01,83009]]");assert(!live::receive(tick));
  primary.socket->emit(WStype_DISCONNECTED,"HTTP 429");primary.step();assert(primary.cooldown[4].availableAt==testMillis+60000);
  primary.step();assert(primary.source==3&&primary.socket->host=="ws.gemini.com");
  std::string gemini="{\"E\":"+std::to_string(uint64_t(testEpoch)*1000000000+987654321)+",\"s\":\"btcusd\",\"t\":2840141032956701,\"p\":\"83017.89\",\"q\":\"0.01\",\"m\":true}";
  primary.socket->emit(WStype_TEXT,gemini);assert(live::receive(tick)&&tick.source==3&&tick.quote.timestampMs==uint64_t(testEpoch)*1000+987);
  primary.socket->emit(WStype_TEXT,gemini);assert(!live::receive(tick));
  primary.socket->emit(WStype_TEXT,"{\"id\":\"1\",\"status\":429,\"error\":{\"code\":-1003,\"msg\":\"Rate limit exceeded\"}}");
  primary.step();assert(primary.cooldown[3].availableAt==testMillis+60000&&!primary.socket);
  primary.nextProvider=3;live::allowUsdt(true);primary.step();
  assert(primary.source==5&&primary.socket->host=="data-stream.binance.vision"&&primary.socket->url=="/ws/btcusdt@aggTrade"&&primary.socket->sent.empty());
  std::string binance="{\"e\":\"aggTrade\",\"s\":\"BTCUSDT\",\"p\":\"83016.54\",\"a\":152345678,\"T\":"+std::to_string(uint64_t(testEpoch)*1000+123)+"}";
  primary.socket->emit(WStype_TEXT,binance);assert(live::receive(tick)&&tick.source==5&&std::abs(tick.quote.price-83016.54)<1e-6);
  primary.socket->emit(WStype_TEXT,binance);assert(!live::receive(tick));
  live::allowUsdt(false);primary.step();assert(!primary.socket||primary.source!=5); // Expired conversion closes an unusable stream and leaves USD failover eligible.
  assert(!live::pauseAndWait(0));steps();assert(live::pauseAndWait(0)&&!liveSockets());live::resume();
  WiFi.connection=0;steps();assert(live::status().paused&&!liveSockets());WiFi.connection=WL_CONNECTED;
  testMillis=UINT32_MAX-500;for(auto& slot:slots){slot.nextProvider=slot.index?2:0;for(auto& state:slot.cooldown)state.availableAt=testMillis;}
  steps();testMillis=501;for(auto& slot:slots)if(slot.source==1)slot.socket->emit(WStype_TEXT,kraken);
  assert(live::fresh(testMillis));
  testMillis+=35001;steps();assert(!live::status().connected);
  // The pinned library begins with lastPing=0 and pongReceived=false. A
  // connected feed must not time out before its first scheduled ping, nor
  // lose a healthy data stream merely because control PONGs are delayed.
  for(auto& slot:slots){slot.close();slot.nextProvider=slot.index?2:0;for(auto& state:slot.cooldown)state.availableAt=testMillis;}
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
  for(auto& slot:slots){slot.close();slot.nextProvider=slot.index?2:0;for(auto& state:slot.cooldown)state.availableAt=testMillis;}
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
  slots[0].nextProvider=4;slots[1].nextProvider=5;live::allowUsdt(false);steps();
  assert(slots[0].source==2&&slots[1].source==3);
  uint32_t fallbackStart=testMillis;testMillis=fallbackStart+30001;
  {btc::TlsLease held;assert(held.acquire());slots[0].step();
    assert(!slots[0].connected&&slots[0].source==0&&slots[1].connected&&recoveryOwner==1);
    slots[1].step();assert(slots[1].connected&&slots[1].source==3&&recoveryOwner==1);
  }
  slots[0].step();assert(slots[0].connected&&slots[0].source==0&&!recoveryOwner);
  assert(live::status().slots[0].reason==live::Reason::PreferredRecovery&&live::status().slots[0].reasonSource==2);
  slots[1].step();assert(slots[1].source==4&&slots[1].connected); // Peer preferred ownership is respected.
  // With only one live fallback, retain it while the idle worker recovers.
  slots[0].close();slots[1].close();slots[0].nextProvider=4;slots[0].step();
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
  currentBookStatusTests();
  convertedBookStatusTests();
  for(auto& slot:slots){slot.close();vQueueDelete(slot.ticks);slot.ticks=nullptr;}assert(!queues&&!liveSockets());
  std::cout<<"PASS: dual live workers, bounded burst queues, USD/USDT parsers, channel binding/deduplication, current-book liveness without synthetic quotes, HTTP memory reservation, uninterrupted primary, failover/backoff, Wi-Fi/SNTP, OTA pause and rollover\n";
}
