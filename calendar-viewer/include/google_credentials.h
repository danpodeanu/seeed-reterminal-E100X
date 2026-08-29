#pragma once

#include <Arduino.h>

namespace google_credentials {

struct Credentials {
  String projectId;
  String privateKeyId;
  String privateKey;
  String clientEmail;
  String tokenUri;
};

bool load(Credentials& out, String& failureReason);
bool storeJson(const String& json, String& failureReason);
bool configured();
String configuredEmail();
bool clear(String& failureReason);

}  // namespace google_credentials
