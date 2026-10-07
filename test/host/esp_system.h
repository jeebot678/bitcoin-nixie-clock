#pragma once
#include "Arduino.h"
inline uint32_t esp_random(){static uint32_t value=123456;value=value*1664525+1013904223;return value;}
