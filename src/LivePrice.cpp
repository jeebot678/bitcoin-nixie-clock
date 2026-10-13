#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <memory>
#include <time.h>
#include "LivePrice.h"
#include "TlsGate.h"
#include "NetworkMemory.h"

extern const uint8_t caBundle[] asm("_binary_data_cert_x509_crt_bundle_bin_start");
namespace {
// Put the highest-throughput USD trade feed on the primary socket. Fallbacks
// add subscriptions, not TLS sessions, and share the same bounded retries.
constexpr uint8_t providers[]={0,4,1,5,2,3};
constexpr unsigned providerCount=sizeof(providers)/sizeof(providers[0]);
constexpr unsigned slotCount=2;
constexpr unsigned tickDepth=16;
std::atomic<bool> pauseRequested{false},busy{false},primaryReserved{false},secondaryReserved{false},usdtAllowed{false};
std::atomic<uint8_t> owners[btc::kSourceCount];
std::atomic<uint8_t> recoveryOwner{0};
bool otherConnected(unsigned index);
class StreamSocket : public WebSocketsClient {
 public:
  int httpStatus() const {return _client.cCode;}
  int tlsError() {
#ifdef ARDUINO
    char ignored[1];return _client.ssl?_client.ssl->lastError(ignored,sizeof(ignored)):0;
#else
    return _client.tlsError;
#endif
  }
  void armHeartbeat(uint32_t now){_client.lastPing=now;_client.pongReceived=true;_client.pongTimeoutCount=0;}
};
struct Slot {
  QueueHandle_t ticks=nullptr;
  std::atomic<bool> paused{true},connected{false},bookValid{false},convertedBookValid{false};
  std::atomic<uint32_t> quotes{0},receivedAt{0},stackFreeBytes{0},dropped{0},rejected{0},disconnects{0};
  std::atomic<uint32_t> sourceQuotes[btc::kSourceCount],sourceReceived[btc::kSourceCount];
  std::atomic<uint16_t> sourceFailures[btc::kSourceCount];
  std::atomic<live::Phase> phase{live::Phase::Paused};
  std::atomic<live::Reason> reason{live::Reason::None};
  std::atomic<uint8_t> reasonSource{255};
  std::atomic<int16_t> diagnosticStatus{0};
  std::atomic<int32_t> diagnosticTls{0};
  std::atomic<uint32_t> reasonAt{0};
  std::unique_ptr<StreamSocket> socket;
  btc::TlsLease handshake;
  btc::SourceState cooldown[btc::kSourceCount];
  StaticJsonDocument<2048> document;
  unsigned index=0,nextProvider=0;
  std::atomic<uint8_t> source{0};
  bool failed=false,closing=false;
  int failureStatus=0;
  uint32_t openedAt=0,lastValidQuoteAt=0,channel=0;
  std::atomic<uint32_t> lastMessageAt{0};
  std::atomic<uint32_t> bookMessageAt[3];
  uint64_t lastTrade=0;
  btc::BitfinexBook book,usdtBook,fxBook;
  bool currentBook(uint32_t now) const {return source==4&&connected&&bookValid&&uint32_t(now-bookMessageAt[0].load())<=35000;}
  bool currentConvertedBook(uint32_t now) const{
    return source==4&&connected&&convertedBookValid&&uint32_t(now-bookMessageAt[1].load())<=35000&&uint32_t(now-bookMessageAt[2].load())<=35000;
  }
  void publishBookValidity(){
    bookValid=!failed&&book.valid();
    convertedBookValid=!failed&&usdtBook.valid()&&fxBook.valid()&&fxBook.bid>=.5&&fxBook.ask<=1.5;
  }
  void enqueueConverted(uint32_t now){
    if(!currentConvertedBook(now))return;
    live::Tick tick;
    tick.quote.price=(usdtBook.bid+usdtBook.ask)/2*((fxBook.bid+fxBook.ask)/2);
    tick.quote.timestampMs=std::max(usdtBook.publishedAt,fxBook.publishedAt);
    tick.quote.timestamp=uint32_t(tick.quote.timestampMs/1000);
    tick.quote.kind=btc::QuoteKind::ConvertedBboMidpoint;
    if(btc::validPrice(tick.quote.price))enqueue(tick,now);
  }
  void record(live::Reason cause,uint32_t now,int code=0,int tls=0){
    reasonSource=source.load();diagnosticStatus=int16_t(code);diagnosticTls=tls;reasonAt=now;reason=cause;
  }
  void enqueue(live::Tick& tick,uint32_t now,bool price=true) {
    tick.source=source;tick.receivedAt=now;
    // Retain actual burst prices for the display selector and OHLC builder.
    // When full, discard the oldest tick instead of blocking network reads.
    if(xQueueSend(ticks,&tick,0)!=pdTRUE){
      live::Tick oldest;xQueueReceive(ticks,&oldest,0);++dropped;
      if(xQueueSend(ticks,&tick,0)!=pdTRUE)return;
    }
    if(price){receivedAt=lastValidQuoteAt=now;++quotes;++sourceQuotes[source];sourceReceived[source]=now;}
    cooldown[source].failures=0;
  }
  void close(bool releaseRecovery=true) {
    bookValid=convertedBookValid=false;
    closing=true;socket.reset();closing=false;connected=false;
    handshake.release();
    uint8_t owner=uint8_t(index+1);owners[source].compare_exchange_strong(owner,0);
    if(releaseRecovery){owner=uint8_t(index+1);recoveryOwner.compare_exchange_strong(owner,0);}
  }
  void event(WStype_t type,uint8_t* payload,size_t length) {
    if (closing) return;
    uint32_t now=millis();
    if (type==WStype_CONNECTED) {
      bookValid=convertedBookValid=false;
      connected=true;lastMessageAt=now;
      socket->armHeartbeat(now);phase=live::Phase::Online;
      uint8_t owner=uint8_t(index+1);recoveryOwner.compare_exchange_strong(owner,0);
      const char* subscription=source==1
        ? "{\"method\":\"subscribe\",\"params\":{\"channel\":\"ticker\",\"symbol\":[\"BTC/USD\",\"USDT/USD\"],\"event_trigger\":\"bbo\",\"snapshot\":true}}"
        : source==0 ? "{\"type\":\"subscribe\",\"product_ids\":[\"BTC-USD\"],\"channels\":[\"ticker\",\"heartbeat\"]}"
        : source==2 ? "{\"event\":\"bts:subscribe\",\"data\":{\"channel\":\"live_trades_btcusd\"}}"
        : source==4 ? "{\"event\":\"subscribe\",\"channel\":\"book\",\"symbol\":\"tBTCUSD\",\"prec\":\"P0\",\"freq\":\"F0\",\"len\":\"1\"}"
        : source==3 ? "{\"id\":\"1\",\"method\":\"subscribe\",\"params\":[\"btcusd@trade\"]}":nullptr;
      if(source==4)socket->sendTXT("{\"event\":\"conf\",\"flags\":32768}");
      if(subscription)socket->sendTXT(subscription);
      if(source==4){
        socket->sendTXT("{\"event\":\"subscribe\",\"channel\":\"book\",\"symbol\":\"tBTCUST\",\"prec\":\"P0\",\"freq\":\"F0\",\"len\":\"1\"}");
        socket->sendTXT("{\"event\":\"subscribe\",\"channel\":\"book\",\"symbol\":\"tUSTUSD\",\"prec\":\"P0\",\"freq\":\"F0\",\"len\":\"1\"}");
      }
      if(source==1)socket->sendTXT("{\"method\":\"subscribe\",\"params\":{\"channel\":\"trade\",\"symbol\":[\"BTC/USD\"],\"snapshot\":false}}");
      Serial.printf("Live stream %u connected: %s\n",index,btc::kSources[source].name);
    } else if (type==WStype_DISCONNECTED || type==WStype_ERROR) {
      char reason[128];size_t count=std::min(length,sizeof(reason)-1);
      if (payload && count) memcpy(reason,payload,count);else count=0;
      reason[count]=0;const char* code=strstr(reason,"HTTP ");
      if (code) failureStatus=atoi(code+5);
      live::Reason cause=code?live::Reason::Http:strstr(reason,"Header response timeout")?live::Reason::HeaderTimeout:
        strstr(reason,"handshake")?live::Reason::Protocol:live::Reason::Transport;
      record(cause,now,failureStatus,socket->tlsError());
      Serial.printf("Stream %u disconnected: %s (%s)\n",index,btc::kSources[source].name,reason);
      bookValid=convertedBookValid=false;connected=false;failed=true;++disconnects;
    } else if (type==WStype_TEXT && length) {
      if(length>8192){++rejected;if(source==4){bookValid=convertedBookValid=false;failed=true;record(live::Reason::Protocol,now);}return;}
      document.clear();
      if (deserializeJson(document,payload,length,DeserializationOption::NestingLimit(8))) {++rejected;if(source==4){bookValid=convertedBookValid=false;failed=true;record(live::Reason::Protocol,now);}return;}
      lastMessageAt=now;
      JsonVariantConst root=document.as<JsonVariantConst>();
      if (btc::equals(root["type"],"error") || btc::equals(root["event"],"error") ||
          btc::equals(root["event"],"bts:request_reconnect") ||
          (!root["success"].isNull() && root["success"]==false) ||
          (source==3 && !root["error"].isNull())) {
        if(source==3)failureStatus=root["status"].as<int>();
        record(btc::equals(root["event"],"bts:request_reconnect")?live::Reason::ServerReconnect:live::Reason::SubscriptionError,now,failureStatus);
        bookValid=convertedBookValid=false;failed=true;return;
      }
      live::Tick tick;
      uint32_t epoch=uint32_t(time(nullptr));
      if(source==4){
        if(btc::equals(root["event"],"conf")||btc::equals(root["event"],"subscribed")){
          bool bound=false;
          if(btc::equals(root["event"],"conf"))bound=book.configure(root)&&usdtBook.configure(root)&&fxBook.configure(root);
          else if(btc::equals(root["symbol"],"tBTCUSD")){bookValid=false;uint32_t id=root["chanId"].as<uint32_t>();bound=id!=usdtBook.channel&&id!=fxBook.channel&&book.bind(root);}
          else if(btc::equals(root["symbol"],"tBTCUST")){convertedBookValid=false;uint32_t id=root["chanId"].as<uint32_t>();bound=id!=book.channel&&id!=fxBook.channel&&usdtBook.bind(root,"tBTCUST");}
          else if(btc::equals(root["symbol"],"tUSTUSD")){convertedBookValid=false;uint32_t id=root["chanId"].as<uint32_t>();bound=id!=book.channel&&id!=usdtBook.channel&&fxBook.bind(root,"tUSTUSD");}
          if(!bound){failed=true;record(live::Reason::SubscriptionError,now);}
          publishBookValidity();
          return;
        }
        if(btc::equals(root["event"],"info")&&(root["code"]==20051||root["code"]==20060||root["code"]==20061)){
          bookValid=convertedBookValid=false;failed=true;record(live::Reason::ServerReconnect,now);return;
        }
        if(btc::equals(root["event"],"unsubscribed")){
          bookValid=convertedBookValid=false;failed=true;record(live::Reason::SubscriptionError,now);return;
        }
        if(!root.is<JsonArrayConst>()||!root[0].is<uint32_t>())return;
        uint32_t id=root[0].as<uint32_t>();btc::BitfinexBook* selected=nullptr;unsigned position=0;
        if(book.channel&&id==book.channel)selected=&book;
        else if(usdtBook.channel&&id==usdtBook.channel){selected=&usdtBook;position=1;}
        else if(fxBook.channel&&id==fxBook.channel){selected=&fxBook;position=2;}
        if(!selected)return;
        bookMessageAt[position]=now;
        bool updated=selected->apply(root,epoch,tick.quote);
        if(selected->corrupted){++rejected;failed=true;record(live::Reason::Protocol,now);}
        publishBookValidity();
        if(updated){if(position==0)enqueue(tick,now);else enqueueConverted(now);}
        return; // Trade/ticker payloads cannot masquerade as a current BBO.
      }
      if(source==1 && btc::parseKrakenFx(root,epoch,tick.fx)) {
        tick.quote.timestamp=btc::isoTimestamp(root["data"][0]["timestamp"]);
        tick.quote.timestampMs=btc::isoTimestampMs(root["data"][0]["timestamp"]);
        enqueue(tick,now,false);return;
      }
      if(source==1 && btc::equals(root["channel"],"trade") && btc::equals(root["type"],"update") && root["data"].is<JsonArrayConst>()){
        for(JsonVariantConst row:root["data"].as<JsonArrayConst>()){
          uint64_t id=0;tick={};
          if(!btc::parseKrakenTrade(row,epoch,tick.quote,id)){++rejected;continue;}
          if(id<=lastTrade)continue;
          lastTrade=id;enqueue(tick,now);
        }
        return;
      }
      if (btc::parseStreamQuote(source,root,uint32_t(time(nullptr)),tick.quote,channel)) {
        uint64_t trade=source==4?root[2][0].as<uint64_t>():source==2?root["data"]["id"].as<uint64_t>():source==3?root["t"].as<uint64_t>():source==5?root["a"].as<uint64_t>():0;
        if ((source==4 || source==2 || source==3 || source==5) && (!trade || trade<=lastTrade)) return;
        if(trade)lastTrade=trade;
        enqueue(tick,now);
      }
    } else if (type==WStype_PONG) lastMessageAt=now;
  }
  void step() {
    if (!ticks) return;
    uint32_t now=millis();paused=false;
    if (pauseRequested || (index==1 && secondaryReserved) || (index==0 && primaryReserved) ||
        WiFi.status()!=WL_CONNECTED || time(nullptr)<config::kMinimumEpoch) {
      if(socket)record(pauseRequested?live::Reason::Pause:WiFi.status()!=WL_CONNECTED?live::Reason::Wifi:
        time(nullptr)<config::kMinimumEpoch?live::Reason::Clock:live::Reason::Reservation,now);
      close();paused=true;phase=live::Phase::Paused;return;
    }
    if(socket&&btc::kSources[source].usdt&&!usdtAllowed){record(live::Reason::FxExpired,now);close();}
    // Upgrade a sparse fallback after its preferred feeds leave cooldown.
    // One worker upgrades at a time, preserving the other established feed.
    if(connected&&source!=providers[0]&&source!=providers[1]&&uint32_t(now-openedAt)>=30000&&!busy&&otherConnected(index)){
      for(unsigned position=0;position<2;++position){
        unsigned preferred=providers[position];
        if(!btc::due(now,cooldown[preferred].availableAt)||owners[preferred])continue;
        uint8_t free=0;
        if(!recoveryOwner.compare_exchange_strong(free,uint8_t(index+1)))break;
        free=0;
        if(!owners[preferred].compare_exchange_strong(free,uint8_t(index+1))){uint8_t own=uint8_t(index+1);recoveryOwner.compare_exchange_strong(own,0);continue;}
        record(live::Reason::PreferredRecovery,now);close(false);source=uint8_t(preferred);nextProvider=position;break;
      }
    }
    if (!socket) {
      if(busy){phase=live::Phase::WaitHttp;return;}
      if(!network_memory::available()){phase=live::Phase::WaitMemory;return;}
      int eligible=-1;
      for (unsigned i=0;i<providerCount;++i) {
        unsigned position=(nextProvider+i)%providerCount;uint8_t candidate=providers[position],empty=0;
        if ((!btc::kSources[candidate].usdt||usdtAllowed) && btc::due(now,cooldown[candidate].availableAt) &&
            (owners[candidate]==uint8_t(index+1)||owners[candidate].compare_exchange_strong(empty,uint8_t(index+1)))) {eligible=int(position);break;}
      }
      if (eligible<0){phase=live::Phase::Backoff;return;}
      nextProvider=unsigned(eligible);source=providers[nextProvider];
      failed=false;failureStatus=0;channel=0;lastTrade=0;book.reset();usdtBook.reset();fxBook.reset();
      for(auto& messageAt:bookMessageAt)messageAt=now;
      openedAt=lastMessageAt=lastValidQuoteAt=now;
      Serial.printf("Stream %u starting: %s, dataHeap=%lu dataBlock=%lu\n",index,btc::kSources[source].name,(unsigned long)network_memory::freeBytes(),(unsigned long)network_memory::largestBlock());
      socket.reset(new StreamSocket());socket->onEvent([this](WStype_t type,uint8_t* data,size_t size){event(type,data,size);});
      const char* host=source==1?"ws.kraken.com":source==0?"ws-feed.exchange.coinbase.com":source==2?"ws.bitstamp.net":source==4?"api-pub.bitfinex.com":source==3?"ws.gemini.com":"data-stream.binance.vision";
      const char* path=source==1?"/v2":source==4?"/ws/2":source==5?"/ws/btcusdt@aggTrade":"/";
      socket->beginSslWithBundle(host,443,path,caBundle,"");
      socket->setReconnectInterval(5000);
      // The library's PONG timer starts at construction, before any ping.
      // Arm it on CONNECTED; valid incoming traffic determines liveness so
      // an active feed cannot be dropped solely for a delayed control PONG.
      socket->enableHeartbeat(20000,10000,0);
    }
    if (!connected) {
      bool started=bool(handshake);
      if (!handshake.acquire()){phase=live::Phase::WaitTls;return;}
      // Another connection can consume the budget while this slot waits for
      // the TLS gate. Recheck AFTER acquiring it, before any SSL allocation.
      if (!started && !network_memory::fits()) {handshake.release();phase=live::Phase::WaitMemory;return;}
      if (!started) openedAt=lastMessageAt=lastValidQuoteAt=now;
      phase=live::Phase::Handshake;
#ifdef ARDUINO
      // Recovery/FX expiry can close an online feed earlier in this same step.
      // Lower priority again before TLS crypto so IDLE0 can feed its watchdog.
      vTaskPrioritySet(nullptr,0);
#endif
      socket->loop();if (connected) handshake.release();
      // connectFailedCb() emits no event. Capture SSL's real negative error
      // before the next loop deletes it, then release the gate for the peer.
      int tls=socket->tlsError();
      if(!connected&&tls<0){failed=true;record(live::Reason::Tls,millis(),socket->httpStatus(),tls);}
    } else socket->loop();
    now=millis();
    if (failed || uint32_t(now-lastMessageAt)>35000 || (uint32_t(now-lastValidQuoteAt)>120000&&!currentBook(now)&&!currentConvertedBook(now)) ||
        (!connected && uint32_t(now-openedAt)>25000)) {
      int code=failureStatus?failureStatus:socket->httpStatus();auto& state=cooldown[source];
      if(!failed)record(uint32_t(now-lastMessageAt)>35000?live::Reason::NoMessages:
        uint32_t(now-lastValidQuoteAt)>120000?live::Reason::NoQuotes:live::Reason::HandshakeDeadline,now,code,socket->tlsError());
      if(sourceFailures[source]!=UINT16_MAX)++sourceFailures[source];
      state.failures=std::min<uint8_t>(state.failures+1,5);
      uint32_t retry=std::min<uint32_t>(60000,5000U<<(state.failures-1));
      if (code==429) retry=std::max<uint32_t>(retry,60000);
      if (code==401 || code==403 || code==451) retry=3600000;
      state.availableAt=now+retry;nextProvider=(nextProvider+1)%providerCount;
      Serial.printf("Live stream retry: %s HTTP %d, backoff %lu ms\n",btc::kSources[source].name,code,(unsigned long)retry);
      close();
      phase=live::Phase::Backoff;
    }
  }
};
Slot slots[slotCount];
bool otherConnected(unsigned index){return slots[(index+1)%slotCount].connected.load();}
void worker(void* argument) {
  auto& slot=*static_cast<Slot*>(argument);
  for (;;) {
#ifdef ARDUINO
    // Established socket reads get the previous release's responsive priority;
    // only connection crypto shares priority with IDLE0 for watchdog health.
    vTaskPrioritySet(nullptr,slot.connected?1:0);
#endif
    slot.step();
#ifdef ARDUINO
    slot.stackFreeBytes=uxTaskGetStackHighWaterMark(nullptr);
#endif
    delay(5);
  }
}
}
namespace live {
bool begin() {
  unsigned started=0;
  for (unsigned i=0;i<slotCount;++i) {
    auto& slot=slots[i];slot.index=i;slot.nextProvider=i;
    slot.ticks=xQueueCreate(tickDepth,sizeof(Tick));
    if (!slot.ticks) continue;
    // A separate worker lets established feeds receive while another opens.
    // Crypto shares IDLE0's priority; it cannot starve the watchdog's idle task.
    // Measured peak usage is about 5.2 KiB; 9 KiB retains >3 KiB headroom.
    if (xTaskCreatePinnedToCore(worker,i?"btc-stream-1":"btc-stream-0",9216,&slot,0,nullptr,0)==pdPASS) ++started;
    else {vQueueDelete(slot.ticks);slot.ticks=nullptr;}
  }
  return started>0;
}
bool receive(Tick& tick) {
  static unsigned next=0;
  for (unsigned i=0;i<slotCount;++i) {
    auto& slot=slots[(next+i)%slotCount];
    if (slot.ticks && xQueueReceive(slot.ticks,&tick,0)==pdTRUE) {next=(slot.index+1)%slotCount;return true;}
  }
  return false;
}
Status status() {
  Status result;
  for(auto& age:result.quoteAges)age=UINT32_MAX;
  for (auto& slot:slots) {
    auto& diagnostic=result.slots[slot.index];
    diagnostic.source=slot.source;diagnostic.phase=slot.paused?Phase::Paused:slot.phase.load();
    diagnostic.reason=slot.reason;diagnostic.reasonSource=slot.reasonSource;
    diagnostic.httpStatus=slot.diagnosticStatus;diagnostic.tlsError=slot.diagnosticTls;
    if(diagnostic.reason!=Reason::None)diagnostic.reasonAgeMs=millis()-slot.reasonAt.load();
    result.connected=result.connected||slot.connected;result.paused=result.paused&&slot.paused;
    result.connections+=slot.connected?1:0;result.quotes+=slot.quotes;
    if(slot.connected)result.providers|=uint16_t(1U<<slot.source.load());
    if(slot.currentBook(millis()))result.currentBooks|=uint16_t(1U<<slot.source.load());
    if(slot.currentConvertedBook(millis()))result.currentConvertedBooks|=uint16_t(1U<<slot.source.load());
    result.dropped+=slot.dropped;result.rejected+=slot.rejected;result.disconnects+=slot.disconnects;
    for(unsigned source=0;source<btc::kSourceCount;++source){
      result.quoteCounts[source]+=slot.sourceQuotes[source];
      uint32_t failures=uint32_t(result.failures[source])+slot.sourceFailures[source].load();
      result.failures[source]=uint16_t(std::min<uint32_t>(UINT16_MAX,failures));
      if(slot.sourceQuotes[source]){
        uint32_t age=millis()-slot.sourceReceived[source].load();
        if(age<result.quoteAges[source])result.quoteAges[source]=age;
      }
    }
    if (slot.quotes && (!result.receivedAt || btc::due(slot.receivedAt,result.receivedAt))) result.receivedAt=slot.receivedAt;
    uint32_t stack=slot.stackFreeBytes;
    if (stack && (!result.stackFreeBytes || stack<result.stackFreeBytes)) result.stackFreeBytes=stack;
  }
  return result;
}
bool fresh(uint32_t now) {
  for (auto& slot:slots) if(slot.currentBook(now)||slot.currentConvertedBook(now)||(slot.connected&&slot.quotes&&uint32_t(now-slot.receivedAt.load())<5000))return true;
  return false;
}
bool pauseAndWait(uint32_t timeoutMs) {
  pauseRequested=true;uint32_t start=millis();
  while (!status().paused && uint32_t(millis()-start)<timeoutMs) delay(5);
  return status().paused;
}
void resume() {pauseRequested=false;}
void httpBusy(bool value) {if (!value){primaryReserved=false;secondaryReserved=false;}busy=value;}
void allowUsdt(bool allowed){usdtAllowed=allowed;}
bool prepareHttp(size_t jsonBytes,uint32_t timeoutMs,bool probe,bool allowPrimaryPause) {
  busy=true;uint32_t start=millis();
  auto fits=[&](){return probe?network_memory::fits(jsonBytes):network_memory::available(jsonBytes);};
  // Mere dispatch holds new handshakes but leaves established subscriptions
  // running. Release the secondary only if the real data RAM needs it.
  if(fits())return true;
  secondaryReserved=true;
  while (!slots[1].paused && uint32_t(millis()-start)<timeoutMs) delay(5);
  if (!slots[1].paused) return false;
  if (!fits()) {
    if(!allowPrimaryPause)return false;
    primaryReserved=true;
    while (!slots[0].paused && uint32_t(millis()-start)<timeoutMs) delay(5);
    if (!slots[0].paused) return false;
  }
  return fits();
}
}
