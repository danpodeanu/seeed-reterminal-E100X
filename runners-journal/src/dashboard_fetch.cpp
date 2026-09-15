#include "dashboard_fetch.h"

#include <Arduino.h>
#include <HTTPClient.h>

#include "config.h"
#include "secrets.h"
#include "trusted_client.h"

namespace dashboard_fetch {

bool fetch(String& body, String& failureReason) {
  failureReason = "";

  tls_client::DefaultRootClient client;
  client.setTimeout(config::HTTP_TIMEOUT_MS);

  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, config::SUPABASE_URL)) {
    failureReason = "Could not start HTTPS request";
    return false;
  }

  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
  http.addHeader("Content-Type", "application/json");

  const int status = http.POST("{}");
  if (status != 200) {
    failureReason = String("HTTP ") + status;
    http.end();
    return false;
  }

  const String response = http.getString();
  http.end();

  if (response.length() == 0) {
    failureReason = "Empty body";
    return false;
  }
  if (static_cast<size_t>(response.length()) > config::MAX_DASHBOARD_BODY_BYTES) {
    failureReason = "Body too large";
    return false;
  }

  body = response;
  return true;
}

}  // namespace dashboard_fetch
