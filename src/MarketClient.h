#pragma once
#include "MarketParsers.h"
#include "CandleStore.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace market {
enum class Kind : uint8_t { Quote, History, Fx, Ota, Candles, SaveCache };
struct Request {
  Kind kind; uint8_t source; uint8_t window; uint32_t epoch, start;
  Request(Kind k=Kind::Quote, uint8_t s=0, uint8_t w=0, uint32_t e=0, uint32_t from=0)
    : kind(k), source(s), window(w), epoch(e), start(from) {}
};
struct Result {
  Request request;
  bool ok = false;
  bool deferred = false;
  int status = 0;
  uint32_t retryAfterMs = 0, receivedAt = 0, epoch = 0;
  btc::Quote quote;
  btc::History history; // Legacy close-cache migration/tests.
  btc::CandleSeries candles[5];
  // Attempted ranges may contain sparse/partial observations; coverage bits
  // additionally establish the historical left boundary from complete trades.
  uint8_t candleAttemptMask=0,candleCoverageMask=0;
  uint64_t candleSnapshotMs=0,candleOldestMs=0,candleNewestMs=0;
  uint64_t cacheEpochMs=0;
};
bool begin();
bool send(const Request& request);
// One immutable snapshot is owned until the response lease is released.
bool sendCache(CandleStore& store,uint8_t window,const btc::CandleSeries& series,const btc::CandleScale& scale,uint64_t epochMs);
// One worker-owned response stays immutable until this lease is destroyed.
// Queue a pointer instead of duplicating history/candle buffers on both cores.
class ResultLease {
 public:
  ResultLease()=default;
  ~ResultLease();
  ResultLease(const ResultLease&)=delete;
  ResultLease& operator=(const ResultLease&)=delete;
  const Result& get()const{return *value_;}
 private:
  const Result* value_=nullptr;
  friend bool receive(ResultLease& result);
};
bool receive(ResultLease& result);
uint32_t stackRemaining();
}
