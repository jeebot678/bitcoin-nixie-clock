#include "SkitDemo.h"
#include <cassert>
int main(){
  assert(skit::kRefreshMs==500);
  for(unsigned t=0;t<15000;t+=500)assert(skit::digits(t)>=80982&&skit::digits(t)<=81018);
  uint32_t previous=skit::digits(15000);
  for(unsigned t=15500;t<=35000;t+=500){auto d=skit::digits(t);assert(d>=previous&&d<=999999);previous=d;}
  assert(skit::price(35000)>1000000);
  assert(skit::digits(35000)==999999&&skit::digits(44500)==999999);
  previous=skit::digits(45000);
  for(unsigned t=45500;t<=48000;t+=500){auto d=skit::digits(t);assert(d<=previous);previous=d;}
  assert(skit::digits(48000)==10&&skit::digits(3600000)==10);
}
