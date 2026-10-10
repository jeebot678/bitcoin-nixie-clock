#pragma once
#include "Arduino.h"
class WiFiClient{
 public:
  std::vector<uint8_t>data;size_t cursor=0;bool live=true;
  int available(){return int(data.size()-cursor);}bool connected(){return live&&cursor<data.size();}
  int read(){return cursor<data.size()?data[cursor++]:-1;}
  int read(uint8_t* buffer,size_t n){size_t count=std::min(n,data.size()-cursor);memcpy(buffer,data.data()+cursor,count);cursor+=count;return int(count);}
  void stop(){live=false;}
};
class WiFiClientSecure:public WiFiClient{
 public:
  bool verified=false;void setCACertBundle(const uint8_t*){verified=true;}void setHandshakeTimeout(unsigned){}void setTimeout(unsigned){}
};
