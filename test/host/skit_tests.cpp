#include "SkitChart.h"
#include <cassert>
int main(){
  assert(skit::kRefreshMs==500);
  for(unsigned t=0;t<15000;t+=500)assert(skit::digits(t)>=80875&&skit::digits(t)<=81125);
  unsigned pullbacks=0,advances=0;
  double previous=skit::price(15000);
  for(unsigned t=15500;t<=35000;t+=500){
    double value=skit::price(t);assert(value>80000&&value<=1050000);
    if(value<previous)++pullbacks;else if(value>previous)++advances;
    previous=value;
  }
  assert(pullbacks>=8&&advances>pullbacks);
  assert(skit::price(35000)>1000000);
  assert(skit::digits(35000)==999999&&skit::digits(44500)==999999);
  for(unsigned t=45000;t<53000;t+=500){
    auto d=skit::digits(t);
    if(t<47000)assert(d>=9880&&d<=10120);
    else if(t<49000)assert(d>=982&&d<=1018);
    else if(t<51000)assert(d>=97&&d<=103);
    else assert(d==5);
  }
  assert(skit::kCycleMs==75000);
  for(unsigned t=0;t<150000;t+=500){
    assert(skit::digits(t)==skit::digits(t+skit::kCycleMs));
    assert(skit::price(t)==skit::price(t+skit::kCycleMs));
  }
  for(unsigned t=53000;t<75000;t+=500)assert(skit::digits(t)==1);
  assert(skit::digits(75000)==81000&&skit::digits(128000)==1);
  assert(config::kWindowSeconds[skit::kWindow]==30);
  btc::CandleSeries series;btc::CandleScale scale;
  skit::seedChart(series,scale);
  auto initial=btc::makeCandles(series,scale,skit::kWindow,skit::kChartStart);
  assert(initial.valid==0x1fffff);
  unsigned low=12,high=0;bool varied=false;
  auto chart=initial.plot();
  for(unsigned c=0;c<21;++c){
    unsigned height=0;
    for(unsigned r=0;r<13;++r)if(btc::plotPixel(chart,r,c)){++height;low=std::min(low,r);high=std::max(high,r);}
    assert(height<=2); // No giant ordinary opening candles.
    if(c&&chart.connectors[c]!=chart.connectors[c-1])varied=true;
  }
  assert(varied&&high-low>=2&&high-low<=4);
  double initialStep=scale.step;
  for(unsigned t=0;t<skit::kCycleMs;t+=500){
    auto stamp=skit::kChartStart+t;
    series.add(stamp,skit::price(t),skit::kWindow,stamp);
    skit::updateScale(series,scale,stamp,t);
    double bottom,top;assert(series.extent(skit::kWindow,stamp,bottom,top));
    assert(scale.low<=bottom&&scale.low+12*scale.step>=top);
    auto frame=btc::makeCandles(series,scale,skit::kWindow,stamp);
    assert(frame.valid==0x1fffff);
    if(t==35000)assert(scale.step>initialStep*100);
  }
  assert(scale.step<initialStep*100); // Reclaim detail as the peak ages out.
}
