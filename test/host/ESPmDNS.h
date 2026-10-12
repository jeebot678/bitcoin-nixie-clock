#pragma once
#include <cassert>
#include "Arduino.h"
struct MdnsMock{bool running=false,fail=false;unsigned starts=0,ends=0;int port=0;
  bool begin(const char* name){assert(std::string(name)=="bitcoin-clock");++starts;return running=!fail;}
  void end(){running=false;++ends;}void addService(const char* service,const char* protocol,int p){assert(std::string(service)=="http"&&std::string(protocol)=="tcp");port=p;}
};
inline MdnsMock& mdnsMock(){static MdnsMock value;return value;}
#define MDNS mdnsMock()
