#pragma once
#include "Arduino.h"
#include <cassert>
#include <cstdlib>
#include <vector>
#include <map>
constexpr unsigned MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
inline uint32_t& testDataHeap(){static uint32_t value=UINT32_MAX;return value;}
inline uint32_t& testDataBlock(){static uint32_t value=UINT32_MAX;return value;}
inline std::vector<size_t>& testHeapBlocks(){static std::vector<size_t> blocks;return blocks;}
struct TestAllocation{size_t size;int block;bool overrideData;};
inline std::map<void*,TestAllocation>& testAllocations(){static std::map<void*,TestAllocation> allocations;return allocations;}
inline size_t heap_caps_get_free_size(unsigned caps){assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));if(!testHeapBlocks().empty()){size_t total=0;for(size_t size:testHeapBlocks())total+=size;return total;}return testDataHeap()==UINT32_MAX?ESP.getFreeHeap():testDataHeap();}
inline size_t heap_caps_get_largest_free_block(unsigned caps){assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));if(!testHeapBlocks().empty())return *std::max_element(testHeapBlocks().begin(),testHeapBlocks().end());return testDataBlock()==UINT32_MAX?ESP.getMaxAllocHeap():testDataBlock();}
inline void* heap_caps_malloc(size_t size,unsigned caps){
  if(size>heap_caps_get_free_size(caps)||size>heap_caps_get_largest_free_block(caps))return nullptr;
  int block=-1;
  if(!testHeapBlocks().empty()){for(size_t i=0;i<testHeapBlocks().size();++i)if(testHeapBlocks()[i]>=size){block=int(i);break;}if(block<0)return nullptr;}
  void* memory=std::malloc(size);if(!memory)return nullptr;
  bool overrideData=testDataHeap()!=UINT32_MAX;testAllocations()[memory]={size,block,overrideData};
  if(block>=0)testHeapBlocks()[block]-=size;else if(overrideData)testDataHeap()-=size;else ESP.freeHeap-=size;
  return memory;
}
inline void heap_caps_free(void* memory){auto allocation=testAllocations().at(memory);if(allocation.block>=0)testHeapBlocks()[allocation.block]+=allocation.size;else if(allocation.overrideData)testDataHeap()+=allocation.size;else ESP.freeHeap+=allocation.size;testAllocations().erase(memory);std::free(memory);}
