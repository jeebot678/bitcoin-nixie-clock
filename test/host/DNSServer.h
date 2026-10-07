#pragma once
#include "WiFi.h"
class DNSServer{public:bool running=false;bool start(int,const char*,IPAddress){running=true;return true;}void stop(){running=false;}void processNextRequest(){}};
