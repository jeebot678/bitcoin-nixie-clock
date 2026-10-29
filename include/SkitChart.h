#pragma once
#include "SkitDemo.h"
#include "CandleChart.h"
namespace skit {
constexpr uint64_t kChartStart=uint64_t(config::kMinimumEpoch+120)*1000;
inline void updateScale(const btc::CandleSeries& series,btc::CandleScale& scale,uint64_t stamp,uint32_t elapsed){
  double low,high;
  if(!series.extent(kWindow,stamp,low,high))return;
  if(elapsed<15000){low=std::min(low,80600.0);high=std::max(high,81400.0);}
  // Fit the actual visible 30s OHLC extent, including the peak during the rug.
  // Reclaim vertical detail when old highs leave the window, without the live
  // firmware's 30-minute smoothing or inventing a logarithmic price axis.
  scale.fit(low,high);scale.smoothedSpan=scale.step*12;
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
