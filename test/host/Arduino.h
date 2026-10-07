#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define PROGMEM
#define FPSTR(x) (x)
constexpr int INPUT=0,OUTPUT=1,INPUT_PULLUP=2,LOW=0,HIGH=1,ADC_11db=3;
class String {
 public:
  std::string value;
  String(){}String(const char* s):value(s?s:""){}String(const std::string& s):value(s){}
  String(unsigned long n):value(std::to_string(n)){}String(unsigned n):value(std::to_string(n)){}
  const char* c_str()const{return value.c_str();}size_t length()const{return value.size();}bool isEmpty()const{return value.empty();}
  String substring(size_t a,size_t b)const{return value.substr(a,b-a);}bool startsWith(const char* s)const{return value.find(s)==0;}
  void replace(const char* from,const String& to){size_t p;while((p=value.find(from))!=std::string::npos)value.replace(p,strlen(from),to.value);}
  void toCharArray(char* dst,size_t n)const{if(n){size_t k=std::min(n-1,value.size());memcpy(dst,value.data(),k);dst[k]=0;}}
  void reserve(size_t n){value.reserve(n);}void trim(){size_t a=value.find_first_not_of(" \r\n\t"),b=value.find_last_not_of(" \r\n\t");value=a==std::string::npos?"":value.substr(a,b-a+1);}
  String& operator+=(char c){value+=c;return *this;}long toInt()const{return strtol(value.c_str(),nullptr,10);}
  size_t write(uint8_t c){value+=char(c);return 1;}size_t write(const uint8_t* s,size_t n){value.append((const char*)s,n);return n;}
};
inline String operator+(const String&a,const String&b){return a.value+b.value;}inline bool operator==(const String&a,const String&b){return a.value==b.value;}inline bool operator!=(const String&a,const String&b){return !(a==b);}
struct SerialMock{std::string input,log;void begin(int){}int available(){return int(input.size());}int read(){char c=input[0];input.erase(0,1);return c;}void println(const char*s){log+=s;log+='\n';}void printf(const char*format,...){char b[2048];va_list args;va_start(args,format);vsnprintf(b,sizeof(b),format,args);va_end(args);log+=b;}};
extern SerialMock Serial;
struct EspMock {uint64_t getEfuseMac()const{return 0x123456789ABC;}unsigned getFreeHeap()const{return 150000;}unsigned getMaxAllocHeap()const{return 100000;}void restart(){++restarts;}int restarts=0;};
extern EspMock ESP;
extern uint32_t testMillis;
extern std::map<int,int> pins,modes,adc;
struct GpioEvent{int pin,value;bool mode;};extern std::vector<GpioEvent> gpioEvents;
inline uint32_t millis(){return testMillis;}inline void delay(uint32_t n){testMillis+=n;}inline void delayMicroseconds(unsigned){}
inline void digitalWrite(uint8_t pin,int value){pins[pin]=value;gpioEvents.push_back({pin,value,false});}
inline void pinMode(uint8_t pin,int mode){modes[pin]=mode;gpioEvents.push_back({pin,mode,true});}
inline int digitalRead(uint8_t pin){return pins[pin];}
inline uint32_t analogReadMilliVolts(uint8_t pin){return adc[pin];}inline void analogReadResolution(int){}inline void analogSetPinAttenuation(uint8_t,int){}
inline void configTime(long,int,const char*,const char*,const char*){}
