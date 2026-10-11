#pragma once
#include <functional>
#include <string>
#include <vector>
#include "Arduino.h"

enum WStype_t {WStype_ERROR,WStype_DISCONNECTED,WStype_CONNECTED,WStype_TEXT,WStype_BIN,WStype_PING,WStype_PONG};
inline unsigned& liveSockets(){static unsigned count=0;return count;}
class WebSocketsClient {
 protected:
  struct Client {int cCode=0;} _client;
 public:
  std::function<void(WStype_t,uint8_t*,size_t)> callback;
  std::string host,url,protocol;std::vector<std::string> sent;
  const uint8_t* ca=nullptr;bool pending=false;
  uint32_t reconnect=0,ping=0,pong=0;unsigned misses=0;
  WebSocketsClient(){++liveSockets();}virtual ~WebSocketsClient(){--liveSockets();}
  void onEvent(std::function<void(WStype_t,uint8_t*,size_t)> event){callback=event;}
  void beginSslWithBundle(const char* hostname,unsigned port,const char* path,const uint8_t* bundle,const char* subprotocol){assert(port==443);host=hostname;url=path;ca=bundle;protocol=subprotocol;pending=true;}
  void setReconnectInterval(uint32_t interval){reconnect=interval;}
  void enableHeartbeat(uint32_t interval,uint32_t timeout,unsigned failures){ping=interval;pong=timeout;misses=failures;}
  bool sendTXT(const char* payload){sent.emplace_back(payload);return true;}
  void loop(){if(pending){pending=false;_client.cCode=101;emit(WStype_CONNECTED,"");}}
  void emit(WStype_t type,const std::string& payload){std::vector<uint8_t> buffer(payload.begin(),payload.end());buffer.push_back(0);callback(type,buffer.data(),payload.size());}
};
