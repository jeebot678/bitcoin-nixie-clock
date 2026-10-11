#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>

// Arduino 2.x Stream::timedRead polls without yielding. A missing/partial TLS
// response must yield to FreeRTOS rather than monopolize the network core.
class CooperativeTlsClient : public WiFiClientSecure {
 public:
  int read(uint8_t* buffer,size_t length) override {
    int count=WiFiClientSecure::read(buffer,length);
    if (length && count<=0) delay(1);
    return count;
  }
  int read() override {
    uint8_t byte;
    return read(&byte,1)==1?byte:-1;
  }
};
