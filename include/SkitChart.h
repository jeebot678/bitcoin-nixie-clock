#pragma once
#include "SkitDemo.h"
#include "CandleChart.h"
namespace skit {
constexpr uint64_t kChartStart=uint64_t(config::kMinimumEpoch+120)*1000;
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
