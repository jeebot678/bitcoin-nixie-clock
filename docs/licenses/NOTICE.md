src/CertificateBundle.cpp is adapted from the Arduino-ESP32 2.0.17 WiFiClientSecure esp_crt_bundle.c implementation.

Copyright 2018–2019 Espressif Systems (Shanghai) PTE LTD. Distributed under the Apache License, Version 2.0; see Apache-2.0.txt.

Project changes recognize a cross-signed trust anchor only when its complete DER subject and public key match the embedded Mozilla bundle, use length-bounded certificate lookup, reject an unavailable bundle, and check unsupported digest algorithms.

Upstream: https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/WiFiClientSecure/src/esp_crt_bundle.c
