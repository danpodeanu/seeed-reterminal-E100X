#include "calendar_provider.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "app_logger.h"
#include "calendar_config_runtime.h"
#include "calendar_http.h"
#include "config.h"
#include "ical_parser.h"
#include "trusted_client.h"

namespace calendar_provider {

bool fetchIcal(const calendar::Window& window, calendar::Data& out,
               String& failureReason, bool bypassHttpCache) {
  failureReason = "";
  const String url = calendar_config::runtime::icalUrl();
  if (!url.startsWith("https://") && !url.startsWith("http://")) {
    failureReason = "Configure an HTTP(S) iCalendar URL";
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  WiFiClient plainClient;
  tls_client::DefaultRootClient secureClient;
  secureClient.setTimeout(config::HTTP_TIMEOUT_MS);
  const bool secure = url.startsWith("https://");
  const bool begun =
      secure ? http.begin(secureClient, url) : http.begin(plainClient, url);
  if (!begun) {
    failureReason = "Could not start the iCalendar request";
    return false;
  }

  http.addHeader("Accept", "text/calendar, text/plain;q=0.9, */*;q=0.1");
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("User-Agent",
                 "reterminal-calendar-viewer/1.0 "
                 "(https://github.com/danpodeanu/seeed-reterminal-E100X)");
  if (bypassHttpCache) {
    http.addHeader("Cache-Control", "no-cache, no-store");
    http.addHeader("Pragma", "no-cache");
  }
  LOG.printf("[calendar] iCalendar GET %s\n", secure ? "(HTTPS URL redacted)"
                                                     : "(HTTP URL redacted)");
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    failureReason = "iCalendar server returned HTTP " + String(status);
    http.end();
    return false;
  }
  String body;
  const bool bodyRead = calendar_http::readBody(
      http, config::MAX_ICAL_BYTES, config::HTTP_TIMEOUT_MS, body,
      failureReason);
  http.end();
  if (!bodyRead) return false;

  std::string parseFailure;
  if (!ical::parse(std::string(body.c_str(), body.length()), window,
                   config::MAX_CALENDAR_EVENTS, out, parseFailure)) {
    failureReason = parseFailure.c_str();
    return false;
  }
  out.fetchedAt = time(nullptr);
  LOG.printf("[calendar] parsed %u event(s) from iCalendar%s\n",
             static_cast<unsigned>(out.events.size()),
             out.truncated ? " (truncated)" : "");
  return true;
}

}  // namespace calendar_provider
