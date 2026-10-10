#pragma once
#include "MarketParsers.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace market {
enum class Kind : uint8_t { Quote, History, Fx, Ota };
struct Request {
  Kind kind; uint8_t source; uint8_t window; uint32_t epoch, start;
  Request(Kind k=Kind::Quote, uint8_t s=0, uint8_t w=0, uint32_t e=0, uint32_t from=0)
    : kind(k), source(s), window(w), epoch(e), start(from) {}
};
struct Result {
  Request request;
  bool ok = false;
  int status = 0;
  uint32_t retryAfterMs = 0, receivedAt = 0, epoch = 0;
  btc::Quote quote;
  btc::History history;
};
bool begin();
bool send(const Request& request);
bool receive(Result& result);
}
