#pragma once
#include <stdint.h>
namespace ota {
enum class Stage : uint8_t { Idle, Manifest, ManifestRejected, Current, AlreadyInstalled,
  RetryLimited, Pausing, PauseFailed, Download, DownloadRejected, DownloadFailed, Installed, Unavailable };
struct Status {
  Stage stage=Stage::Idle;
  uint32_t checkedAt=0,received=0,total=0,retryAfter=0;
  int httpStatus=0;
  char candidate[18]={};
};
Status status();
inline const char* stageName(Stage stage) {
  switch(stage) {
    case Stage::Manifest:return "checking-manifest";
    case Stage::ManifestRejected:return "manifest-rejected";
    case Stage::Current:return "current";
    case Stage::AlreadyInstalled:return "already-installed";
    case Stage::RetryLimited:return "retry-limited";
    case Stage::Pausing:return "pausing-feeds";
    case Stage::PauseFailed:return "feed-pause-failed";
    case Stage::Download:return "downloading";
    case Stage::DownloadRejected:return "download-rejected";
    case Stage::DownloadFailed:return "download-failed";
    case Stage::Installed:return "installed";
    case Stage::Unavailable:return "no-update-slot";
    default:return "idle";
  }
}
void begin();
void healthCheck(uint32_t now,bool healthy);
bool bootConfirmed();
bool checkDue(uint32_t now,bool online);
void defer(uint32_t now);
// Called by the shared network worker: one TLS connection at a time.
bool checkAndInstall();
}
