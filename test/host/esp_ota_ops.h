#pragma once
#include "Arduino.h"
constexpr int ESP_OK=0,ESP_OTA_IMG_PENDING_VERIFY=1,ESP_OTA_IMG_VALID=2;
using esp_ota_img_states_t=int;
struct esp_partition_t{uint32_t size;};
extern int bootState,bootConfirms,bootRollbacks;
inline const esp_partition_t*esp_ota_get_running_partition(){static esp_partition_t p={0x1E0000};return &p;}
inline const esp_partition_t*esp_ota_get_next_update_partition(void*){return esp_ota_get_running_partition();}
inline int esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t*out){*out=bootState;return ESP_OK;}
inline int esp_ota_mark_app_valid_cancel_rollback(){++bootConfirms;bootState=ESP_OTA_IMG_VALID;return ESP_OK;}
inline int esp_ota_mark_app_invalid_rollback_and_reboot(){++bootRollbacks;ESP.restart();return ESP_OK;}
