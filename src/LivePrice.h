#pragma once
#include "StreamParsers.h"

namespace live {
struct Tick { btc::Quote quote; uint32_t receivedAt=0; uint8_t source=0; double fx=0; };
enum class Phase:uint8_t {Paused,WaitHttp,WaitMemory,WaitTls,Handshake,Online,Backoff};
enum class Reason:uint8_t {None,Transport,HeaderTimeout,Http,Protocol,PongTimeout,WriteFailure,ServerReconnect,SubscriptionError,NoMessages,NoQuotes,HandshakeDeadline,Tls,Reservation,Pause,Wifi,Clock,FxExpired,PreferredRecovery};
inline const char* phaseName(Phase phase){
  const char* names[]={"paused","waiting-http","waiting-memory","waiting-tls","handshake","online","backoff"};
  unsigned index=unsigned(phase);return index<sizeof(names)/sizeof(names[0])?names[index]:"unknown";
}
inline const char* reasonName(Reason reason){
  const char* names[]={"none","transport","header-timeout","http","protocol","pong-timeout","write-failure","server-reconnect","subscription-error","no-messages","no-quotes","handshake-deadline","tls","memory-reservation","pause","wifi","clock","fx-expired","preferred-recovery"};
  unsigned index=unsigned(reason);return index<sizeof(names)/sizeof(names[0])?names[index]:"unknown";
}
struct SlotStatus {
  uint8_t source=255,reasonSource=255;
  Phase phase=Phase::Paused;Reason reason=Reason::None;
  int16_t httpStatus=0;int32_t tlsError=0;
  uint32_t reasonAgeMs=UINT32_MAX;
};
struct Status {
  bool connected=false, paused=true;
  uint32_t quotes=0, receivedAt=0, stackFreeBytes=0,dropped=0,rejected=0,disconnects=0;
  uint8_t connections=0;
  uint16_t providers=0,currentBooks=0,currentConvertedBooks=0;
  uint32_t quoteCounts[btc::kSourceCount]={},quoteAges[btc::kSourceCount]={};
  uint16_t failures[btc::kSourceCount]={};
  SlotStatus slots[2];
};
bool begin();
bool receive(Tick& tick);
Status status();
bool fresh(uint32_t now);
// OTA releases the stream TLS allocation before downloading another image.
bool pauseAndWait(uint32_t timeoutMs);
void resume();
void httpBusy(bool busy);
// USDT streams are eligible only while main has a validated, fresh USD rate.
void allowUsdt(bool allowed);
// Reserve bounded HTTP memory by releasing the secondary socket first.
bool prepareHttp(size_t jsonBytes,uint32_t timeoutMs,bool probe=false,bool allowPrimaryPause=true);
}
