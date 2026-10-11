#pragma once
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>

// Force-included only when compiling the pinned WebSocketsClient.cpp. Parse
// its public types first, then replace its TLS allocation type with this
// subclass; public structure layouts and all certificate checks stay intact.
class BoundedWebSocketTls : public WiFiClientSecure {
 public:
  BoundedWebSocketTls() {setHandshakeTimeout(6);setTimeout(3);}
};
#undef WEBSOCKETS_NETWORK_SSL_CLASS
#define WEBSOCKETS_NETWORK_SSL_CLASS BoundedWebSocketTls
