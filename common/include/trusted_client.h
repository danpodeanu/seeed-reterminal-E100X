#pragma once

#include <WiFiClientSecure.h>

namespace tls_client {

// Arduino-ESP32 exposes the ESP-IDF default Mozilla root bundle internally,
// but its public setCACertBundle() overload accepts only custom bundle bytes.
// Attach the framework's configured default bundle through the protected
// client context so arbitrary public HTTPS endpoints still verify their chain.
class DefaultRootClient : public WiFiClientSecure {
 public:
  DefaultRootClient() {
    attach_ssl_certificate_bundle(sslclient.get(), true);
    _use_ca_bundle = true;
    _use_insecure = false;
  }
};

}  // namespace tls_client
