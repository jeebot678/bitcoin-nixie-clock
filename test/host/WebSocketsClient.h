#pragma once
#include <functional>
#include <string>
#include <vector>
#include "Arduino.h"

enum WStype_t {WStype_ERROR,WStype_DISCONNECTED,WStype_CONNECTED,WStype_TEXT,WStype_BIN,WStype_PING,WStype_PONG};
inline unsigned& liveSockets(){static unsigned count=0;return count;}
inline std::function<void()>& connectedHook(){static std::function<void()> callback;return callback;}
inline std::function<void(bool)>& socketLoopHook(){static std::function<void(bool)> callback;return callback;}
class WebSocketsClient {
 protected:
  struct TlsErrorMock {int error=0;int lastError(char*,size_t){return error;}};
  struct Client {int cCode=0,tlsError=0;uint32_t lastPing=0;bool pongReceived=false;unsigned pongTimeoutCount=0;TlsErrorMock tls;TlsErrorMock* ssl=&tls;} _client;
 public:
  std::function<void(WStype_t,uint8_t*,size_t)> callback;
  std::string host,url,protocol;std::vector<std::string> sent;
  const uint8_t* ca=nullptr;bool pending=false,connected=false;
  uint32_t reconnect=0,ping=0,pong=0;unsigned misses=0;
  WebSocketsClient(){++liveSockets();}virtual ~WebSocketsClient(){--liveSockets();}
  void onEvent(std::function<void(WStype_t,uint8_t*,size_t)> event){callback=event;}
  void beginSslWithBundle(const char* hostname,unsigned port,const char* path,const uint8_t* bundle,const char* subprotocol){assert(port==443);host=hostname;url=path;ca=bundle;protocol=subprotocol;pending=true;}
  void setReconnectInterval(uint32_t interval){reconnect=interval;}
  void enableHeartbeat(uint32_t interval,uint32_t timeout,unsigned failures){ping=interval;pong=timeout;misses=failures;_client.pongReceived=false;}
  bool sendTXT(const char* payload){sent.emplace_back(payload);return true;}
  void tlsFailure(int code){pending=false;_client.tlsError=code;_client.tls.error=code;}
  void loop(){
    if(socketLoopHook())socketLoopHook()(pending);
    if(pending){pending=false;_client.cCode=101;emit(WStype_CONNECTED,"");if(connectedHook())connectedHook()();return;}
    // Model the pinned library's ordering: ping first, then PONG timeout.
    if(connected&&ping){
      uint32_t elapsed=millis()-_client.lastPing;
      if(elapsed>ping){_client.lastPing=millis();_client.pongReceived=false;elapsed=0;}
      if(_client.pongReceived)_client.pongTimeoutCount=0;
      else if(elapsed>pong){++_client.pongTimeoutCount;_client.lastPing=millis()-ping-500;if(misses&&_client.pongTimeoutCount>=misses)emit(WStype_DISCONNECTED,"");}
    }
  }
  void emit(WStype_t type,const std::string& payload){
    if(type==WStype_CONNECTED)connected=true;
    if(type==WStype_DISCONNECTED||type==WStype_ERROR)connected=false;
    if(type==WStype_PONG)_client.pongReceived=true;
    std::vector<uint8_t> buffer(payload.begin(),payload.end());buffer.push_back(0);callback(type,buffer.data(),payload.size());
  }
};
