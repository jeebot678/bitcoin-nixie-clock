#pragma once
#include <stdint.h>
#include "OtaUpdate.h"
#include "PriceSources.h"
#include "LivePrice.h"

namespace telemetry {
enum class Command : uint8_t { Ota, Tubes, Readings, Chart, Cadence };
struct Dial {
  uint16_t mv=0,raw=0;
  int8_t decoded=-1,selected=0;
  bool initialized=false;
};
struct Snapshot {
  uint32_t uptime=0,heap=0,largest=0,quotes=0,accepted=0,frames=0;
  uint32_t dataHeap=0,dataBlock=0,httpStack=0;
  uint32_t transitions=0,priceChanges=0,multiChanges=0;
  uint32_t priceReversals=0,selectionHolds=0;
  uint32_t estimatedFrames=0;
  bool displayEstimated=false;
  uint16_t streamProviders=0,currentBooks=0,currentConvertedBooks=0;
  uint32_t streamDropped=0,streamRejected=0,streamDisconnects=0,streamStack=0;
  uint32_t sourceQuotes[btc::kSourceCount]={},sourceAges[btc::kSourceCount]={};
  uint16_t sourceFailures[btc::kSourceCount]={};
  live::SlotStatus streamSlots[2];
  uint32_t chartColumns=0,historySavedAt=0;
  uint16_t chartPixels=0,historyCandles=0;
  int httpStatus=0;
  uint8_t httpKind=255,httpSource=255;
  bool quoteRequested=false;
  uint32_t quoteAge=0,frameAge=0,frameGap=0,nextFrame=0,interval=500,window=300;
  uint32_t minimumGap=0,maximumGap=0;
  uint32_t digits=0;
  double marketPrice=0,displayPrice=0,axisLow=0,axisStep=0;
  Dial frequency,range;
  ota::Status ota;
  uint8_t connections=0,source=0;
  btc::QuoteKind quoteKind=btc::QuoteKind::LastTrade;
  uint8_t chartSource=255;
  uint32_t chartQuoteAge=UINT32_MAX,displayQuoteAge=UINT32_MAX;
  uint8_t displayMode=1,testStep=0,matrixPage=255;
  bool online=false,portal=false,clock=false,confirmed=false,inflight=false,otaPending=false,bootConfirmed=false;
  bool cadenceTest=false;
};
bool begin();
void publish(const Snapshot& snapshot);
bool receive(Command& command);
}
