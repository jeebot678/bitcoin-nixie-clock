#pragma once
#include "Arduino.h"
#include <cassert>
constexpr int HSPI=1,VSPI=2,MSBFIRST=1,SPI_MODE0=0;
struct SpiPacket{int bus;std::vector<uint8_t>bytes;int selected;};extern std::vector<SpiPacket>packets;
class SPISettings{public:SPISettings(int f,int o,int m){assert(f==1000000&&o==MSBFIRST&&m==SPI_MODE0);}};
class SPIClass{
 public:
  int bus;explicit SPIClass(int b):bus(b){}void begin(int,int,int,int){}
  void beginTransaction(SPISettings){packets.push_back({bus,{},-1});}
  uint8_t transfer(uint8_t byte){auto&p=packets.back();p.bytes.push_back(byte);if(bus==VSPI){int selected=-1;for(int cs:{16,17,21,22,25,26})if(pins[cs]==LOW){assert(selected==-1);selected=cs;}assert(selected>=0);p.selected=selected;}else assert(pins[27]==LOW);return 0;}
  void endTransaction(){if(bus==VSPI)for(int cs:{16,17,21,22,25,26})assert(pins[cs]==HIGH);else assert(pins[27]==HIGH);}
};
