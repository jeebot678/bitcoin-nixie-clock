#pragma once
#include <stdint.h>
#include <algorithm>
#include <esp_heap_caps.h>

namespace network_memory {
// ESP.getFreeHeap()/getMaxAllocHeap() include 32-bit-only instruction RAM.
// mbedTLS buffers, JSON and network payloads need byte-addressable data RAM.
inline uint32_t freeBytes(){return heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);}
inline uint32_t largestBlock(){return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);}
constexpr uint32_t kHandshakeBytes=52000,kRecordBuffer=18000;
inline bool available(size_t jsonBytes=0){
  return freeBytes()>=kHandshakeBytes+jsonBytes &&
    largestBlock()>=std::max<size_t>(kRecordBuffer,jsonBytes);
}
// Call under the TLS gate. The two TLS record buffers need separate blocks,
// not one combined 36 KiB block. Probe their real allocation layout after a
// prospective JSON allocation, then return the memory before creating TLS.
inline bool fits(size_t jsonBytes=0){
  if(!available(jsonBytes))return false;
  const unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  void* json=jsonBytes?heap_caps_malloc(jsonBytes,caps):nullptr;
  if(jsonBytes&&!json)return false;
  void* input=heap_caps_malloc(kRecordBuffer,caps);
  void* output=input?heap_caps_malloc(kRecordBuffer,caps):nullptr;
  bool ok=input&&output;
  if(output)heap_caps_free(output);
  if(input)heap_caps_free(input);
  if(json)heap_caps_free(json);
  return ok;
}
}
