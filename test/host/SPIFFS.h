#pragma once
#include "Arduino.h"

struct FsMock {
  std::map<std::string,std::vector<uint8_t>> files;
  bool failMount=false,failOpen=false;
  size_t writeBudget=SIZE_MAX,writes=0;
};
inline FsMock& fsMock(){static FsMock fs;return fs;}
class File {
 public:
  File(){}File(const char*path,bool write):path_(path),valid_(!fsMock().failOpen&&(write||fsMock().files.count(path))){
    if(valid_&&write)fsMock().files[path].clear();
  }
  explicit operator bool()const{return valid_;}
  size_t size()const{return valid_?fsMock().files[path_].size():0;}
  size_t readBytes(char*out,size_t size){
    if(!valid_)return 0;
    auto&data=fsMock().files[path_];size_t count=std::min(size,data.size()-cursor_);
    memcpy(out,data.data()+cursor_,count);cursor_+=count;return count;
  }
  size_t write(const uint8_t*data,size_t size){
    if(!valid_)return 0;
    size_t count=std::min(size,fsMock().writeBudget);fsMock().writeBudget-=count;
    auto&bytes=fsMock().files[path_];bytes.insert(bytes.end(),data,data+count);++fsMock().writes;return count;
  }
  void flush(){}void close(){valid_=false;}
 private:
  std::string path_;size_t cursor_=0;bool valid_=false;
};
struct SpiffsMock {
  bool begin(bool){return !fsMock().failMount;}
  File open(const char*path,const char*mode){return File(path,mode[0]=='w');}
};
inline SpiffsMock& spiffsMock(){static SpiffsMock fs;return fs;}
#define SPIFFS spiffsMock()
