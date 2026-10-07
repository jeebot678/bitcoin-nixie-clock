#pragma once
#include "Arduino.h"
constexpr int U_FLASH=0;
struct UpdateMock{
  bool failBegin=false,failWrite=false,failEnd=false,activated=false;int aborts=0;size_t size=0;std::vector<uint8_t>staging;
  bool begin(size_t n,int){size=n;staging.clear();activated=false;return !failBegin;}
  size_t write(uint8_t*bytes,size_t n){if(failWrite)return 0;staging.insert(staging.end(),bytes,bytes+n);return n;}
  bool end(bool){if(failEnd||staging.size()!=size)return false;activated=true;return true;}
  bool isFinished(){return activated;}void abort(){++aborts;staging.clear();activated=false;}
};extern UpdateMock Update;
