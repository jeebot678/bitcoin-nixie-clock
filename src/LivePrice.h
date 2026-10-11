#pragma once
#include "StreamParsers.h"

namespace live {
struct Tick { btc::Quote quote; uint32_t receivedAt=0; uint8_t source=0; };
struct Status { bool connected=false, paused=true; uint32_t quotes=0, receivedAt=0, stackFreeBytes=0; uint8_t connections=0; };
bool begin();
bool receive(Tick& tick);
Status status();
bool fresh(uint32_t now);
// OTA releases the stream TLS allocation before downloading another image.
bool pauseAndWait(uint32_t timeoutMs);
void resume();
void httpBusy(bool busy);
// Reserve bounded HTTP memory by releasing the secondary socket first.
bool prepareHttp(size_t jsonBytes,uint32_t timeoutMs);
}
