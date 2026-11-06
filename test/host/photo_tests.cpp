#include "PhotoDemo.h"
#include <cassert>
int main(){
  assert(photo::kPrice==456081);
  auto chart=photo::chart();assert(chart.valid==0x1fffff);
  assert(chart.connectors[0]&(1U<<11));assert(chart.connectors[20]&(1U<<1));
  for(unsigned c=0;c<21;++c){
    assert(chart.connectors[c]);
    if(c)assert(chart.connectors[c]&chart.connectors[c-1]);
  }
  for(unsigned i=0;i<100;++i){auto again=photo::chart();
    for(unsigned c=0;c<21;++c)assert(again.connectors[c]==chart.connectors[c]);
  }
}
