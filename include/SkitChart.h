#pragma once
#include "SkitDemo.h"
#include "CandleChart.h"
namespace skit {
constexpr uint64_t kChartStart=uint64_t(config::kMinimumEpoch+120)*1000;
inline void updateScale(const btc::CandleSeries& series,btc::CandleScale& scale,uint64_t stamp,uint32_t elapsed){
  double low,high;
  if(!series.extent(kWindow,stamp,low,high))return;
  if(elapsed<15000){low=std::min(low,80600.0);high=std::max(high,81400.0);}
  // Keep the pre-rally history at the bottom and give the highest observed
  // candle eleven rows of rise. Reserve only one row above the current high,
  // rather than reserving room for a future multimillion-dollar maximum.
  if(elapsed>=15000&&elapsed<45000){
    scale.low=std::min(low,80800.0);
    scale.step=std::ceil(std::max(800.0,high-scale.low)/11*100)/100;
    scale.smoothedSpan=scale.step*12;return;
  }
  // Fit the actual visible 30s OHLC extent, including the peak during the rug.
  // Reclaim vertical detail when old highs leave the window, without the live
  // firmware's 30-minute smoothing or inventing a logarithmic price axis.
  scale.fit(low,high);scale.smoothedSpan=scale.step*12;
}
inline btc::Plot connectedPlot(const btc::CandleFrame& frame){
  auto plot=frame.plot();
  for(unsigned c=1;c<21;++c){
    if(!(plot.valid&(1U<<c))||!(plot.valid&(1U<<(c-1))))continue;
    uint16_t left=plot.connectors[c-1],right=plot.connectors[c];
    if(left&right)continue;
    unsigned distance=13,from=0,to=0;
    for(unsigned a=0;a<13;++a)if(left&(1U<<a))
      for(unsigned b=0;b<13;++b)if(right&(1U<<b)){
        unsigned gap=a>b?a-b:b-a;
        if(gap<distance){distance=gap;from=a;to=b;}
      }
    // Display-only bridge between neighboring candle edges; OHLC is unchanged.
    if(distance<13)plot.connectors[c]|=btc::candleMask(from,to);
  }
  return plot;
}
inline void seedChart(btc::CandleSeries& series,btc::CandleScale& scale){
  series=btc::CandleSeries{};scale=btc::CandleScale{};
  // Continuous pre-roll, rather than identical prices across the opening chart.
  for(int32_t t=-30000;t<=0;t+=500)
    series.add(uint64_t(int64_t(kChartStart)+t),baseline(t),kWindow,kChartStart);
  // A broad initial view keeps ordinary small moves to modest candle heights.
  // The existing spike expansion then compresses these candles as price grows.
  scale.fit(80600,81400);scale.smoothedSpan=scale.step*12;
  scale.update(series,kWindow,kChartStart);
}
}
