#pragma once
#include "Arduino.h"
struct PrefMock{std::map<std::string,std::vector<uint8_t>> blobs;std::map<std::string,String> strings;bool failWrite=false;int writes=0;};
inline PrefMock& prefMock(){static PrefMock p;return p;}
class Preferences{
 public:
  std::string ns;bool begin(const char*n,bool){ns=n;return true;}void end(){}
  std::string key(const char*k){return ns+":"+k;}
  String getString(const char*k,const char*defaultValue){auto i=prefMock().strings.find(key(k));return i==prefMock().strings.end()?String(defaultValue):i->second;}
  size_t putString(const char*k,const String&v){if(prefMock().failWrite)return 0;prefMock().strings[key(k)]=v;++prefMock().writes;return v.length();}
  size_t getBytesLength(const char*k){return prefMock().blobs[key(k)].size();}
  size_t getBytes(const char*k,void*p,size_t n){auto&b=prefMock().blobs[key(k)];size_t count=std::min(n,b.size());memcpy(p,b.data(),count);return count;}
  size_t putBytes(const char*k,const void*p,size_t n){if(prefMock().failWrite)return 0;prefMock().blobs[key(k)]=std::vector<uint8_t>((const uint8_t*)p,(const uint8_t*)p+n);++prefMock().writes;return n;}
  bool remove(const char*k){prefMock().blobs.erase(key(k));prefMock().strings.erase(key(k));return true;}
  uint32_t getUInt(const char*k,uint32_t fallback){auto i=prefMock().strings.find(key(k));return i==prefMock().strings.end()?fallback:uint32_t(i->second.toInt());}
  size_t putUInt(const char*k,uint32_t value){return putString(k,String(value));}
};
