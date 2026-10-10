#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace tls_bundle {
// The embedded bundle is sorted by complete DER subject names.
inline const uint8_t* find(const uint8_t* const* entries, uint16_t count,
                           const uint8_t* name, size_t length) {
  if (!entries || !name || !length) return nullptr;
  int low=0, high=int(count)-1;
  while (low<=high) {
    int middle=low+(high-low)/2;
    const uint8_t* entry=entries[middle];
    size_t subjectLength=(size_t(entry[0])<<8)|entry[1];
    int order=std::memcmp(name,entry+4,std::min(length,subjectLength));
    if (!order) order=length<subjectLength?-1:length>subjectLength?1:0;
    if (!order) return entry;
    if (order<0) high=middle-1; else low=middle+1;
  }
  return nullptr;
}
inline bool sameKey(const uint8_t* entry, const uint8_t* key, size_t length) {
  if (!entry || !key) return false;
  size_t subjectLength=(size_t(entry[0])<<8)|entry[1];
  size_t keyLength=(size_t(entry[2])<<8)|entry[3];
  return length==keyLength && std::memcmp(key,entry+4+subjectLength,length)==0;
}
}
