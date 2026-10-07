#pragma once
#include <stdint.h>
namespace ota {
void begin();
void healthCheck(uint32_t now,bool healthy);
bool checkDue(uint32_t now,bool online);
void defer(uint32_t now);
// Called by the shared network worker: one TLS connection at a time.
bool checkAndInstall();
}
