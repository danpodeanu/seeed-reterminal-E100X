#include "net_http.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "app_logger.h"
#include "sd_card.h"

namespace net_http {
namespace {

inline uint32_t applyBound(uint32_t raw, BoundTimeoutFn bound) {
  return bound == nullptr ? raw : bound(raw);
}

inline bool aborted(ShouldAbortFn abort) {
  return abort != nullptr && abort();
}

}  // namespace

bool getString(const String& url, String& body, uint32_t timeoutMs,
               ShouldAbortFn shouldAbort, BoundTimeoutFn boundTimeout) {
  if (aborted(shouldAbort)) return false;
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(applyBound(timeoutMs, boundTimeout));
  HTTPClient http;
  http.setConnectTimeout(applyBound(timeoutMs, boundTimeout));
  http.setTimeout(applyBound(timeoutMs, boundTimeout));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  LOG.printf("[http] GET %s\n", url.c_str());
  const int status = http.GET();
  if (aborted(shouldAbort)) {
    http.end();
    return false;
  }
  if (status != HTTP_CODE_OK) {
    LOG.printf("[http] GET %s -> %d\n", url.c_str(), status);
    http.end();
    return false;
  }
  client.setTimeout(applyBound(timeoutMs, boundTimeout));
  body = http.getString();
  http.end();
  return !aborted(shouldAbort) && !body.isEmpty();
}

bool downloadToSd(const String& url, const String& destination,
                  uint32_t connectTimeoutMs, uint32_t idleTimeoutMs,
                  size_t maxBytes, ShouldAbortFn shouldAbort,
                  BoundTimeoutFn boundTimeout) {
  if (aborted(shouldAbort)) return false;
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(applyBound(idleTimeoutMs, boundTimeout));
  HTTPClient http;
  http.setConnectTimeout(applyBound(connectTimeoutMs, boundTimeout));
  http.setTimeout(applyBound(idleTimeoutMs, boundTimeout));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  LOG.printf("[http] image GET %s\n", url.c_str());

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    LOG.printf("[http] image GET -> %d\n", status);
    http.end();
    return false;
  }
  const int declaredSize = http.getSize();
  if (declaredSize > static_cast<int>(maxBytes)) {
    LOG.printf("[http] image is too large to cache: %d bytes\n",
               declaredSize);
    http.end();
    return false;
  }

  const String temporary = destination + ".part";
  sd_card::removeFile(temporary);
  File file = sd_card::openForWrite(temporary);
  if (!file) {
    LOG.printf("[cache] could not create %s\n", temporary.c_str());
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  constexpr size_t bufferSize = 4096;
  uint8_t* buffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!buffer) {
    LOG.println("[http] could not allocate SD download buffer");
    file.close();
    sd_card::removeFile(temporary);
    http.end();
    return false;
  }
  size_t total = 0;
  uint32_t lastDataAt = millis();
  bool ok = true;

  while (!aborted(shouldAbort) && http.connected() &&
         (declaredSize < 0 ||
          total < static_cast<size_t>(declaredSize))) {
    const size_t available = stream->available();
    if (available > 0) {
      const size_t wanted = available < bufferSize ? available : bufferSize;
      stream->setTimeout(applyBound(idleTimeoutMs, boundTimeout));
      const int received = stream->readBytes(buffer, wanted);
      if (received <= 0 ||
          file.write(buffer, received) !=
              static_cast<size_t>(received)) {
        ok = false;
        break;
      }
      total += received;
      lastDataAt = millis();
      if (total > maxBytes) {
        LOG.println("[http] image exceeded download limit");
        ok = false;
        break;
      }
      // Let the network and idle tasks run and feed the task watchdog
      // even when both the server and SD card can sustain a
      // continuous stream.
      delay(1);
    } else {
      if (millis() - lastDataAt > idleTimeoutMs) {
        LOG.println("[http] image download timed out");
        ok = false;
        break;
      }
      delay(5);
    }
  }
  if (aborted(shouldAbort)) ok = false;
  if (declaredSize >= 0 && total != static_cast<size_t>(declaredSize))
    ok = false;
  free(buffer);
  file.flush();
  file.close();
  http.end();

  if (!ok || total == 0) {
    LOG.printf("[cache] write/download failed for %s after %lu bytes\n",
               destination.c_str(), static_cast<unsigned long>(total));
    sd_card::removeFile(temporary);
    return false;
  }
  sd_card::removeFile(destination);
  if (!sd_card::renameFile(temporary, destination)) {
    LOG.printf("[cache] could not install %s\n", destination.c_str());
    sd_card::removeFile(temporary);
    return false;
  }
  LOG.printf("[cache] saved %s (%lu bytes)\n", destination.c_str(),
             static_cast<unsigned long>(total));
  return true;
}

bool downloadToMemory(const String& url, uint8_t*& output,
                      size_t& outputLength, uint32_t connectTimeoutMs,
                      uint32_t idleTimeoutMs, size_t maxBytes,
                      ShouldAbortFn shouldAbort,
                      BoundTimeoutFn boundTimeout) {
  output = nullptr;
  outputLength = 0;
  if (aborted(shouldAbort)) return false;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(applyBound(idleTimeoutMs, boundTimeout));
  HTTPClient http;
  http.setConnectTimeout(applyBound(connectTimeoutMs, boundTimeout));
  http.setTimeout(applyBound(idleTimeoutMs, boundTimeout));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  LOG.printf("[http] live image GET %s\n", url.c_str());

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    LOG.printf("[http] live image GET -> %d\n", status);
    http.end();
    return false;
  }

  const int declaredSize = http.getSize();
  if (declaredSize > static_cast<int>(maxBytes)) {
    LOG.printf("[http] live image is too large for PSRAM: %d bytes\n",
               declaredSize);
    http.end();
    return false;
  }

  size_t capacity =
      declaredSize > 0 ? static_cast<size_t>(declaredSize) : 128U * 1024U;
  uint8_t* data = static_cast<uint8_t*>(ps_malloc(capacity));
  if (!data) {
    LOG.printf("[http] could not allocate %lu bytes for live image\n",
               static_cast<unsigned long>(capacity));
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t total = 0;
  uint32_t lastDataAt = millis();
  bool ok = true;

  while (!aborted(shouldAbort) && http.connected() &&
         (declaredSize < 0 ||
          total < static_cast<size_t>(declaredSize))) {
    size_t available = stream->available();
    if (available > 0) {
      if (total + available > maxBytes) {
        LOG.println("[http] live image exceeded PSRAM download limit");
        ok = false;
        break;
      }
      if (total + available > capacity) {
        size_t expanded = capacity;
        while (expanded < total + available) expanded *= 2;
        if (expanded > maxBytes) expanded = maxBytes;
        uint8_t* resized =
            static_cast<uint8_t*>(ps_realloc(data, expanded));
        if (!resized) {
          LOG.println("[http] could not grow live-image PSRAM buffer");
          ok = false;
          break;
        }
        data = resized;
        capacity = expanded;
      }
      const size_t wanted = available < 4096 ? available : 4096;
      stream->setTimeout(applyBound(idleTimeoutMs, boundTimeout));
      const int received = stream->readBytes(data + total, wanted);
      if (received <= 0) {
        ok = false;
        break;
      }
      total += received;
      lastDataAt = millis();
    } else {
      if (millis() - lastDataAt > idleTimeoutMs) {
        LOG.println("[http] live image download timed out");
        ok = false;
        break;
      }
      delay(5);
    }
  }
  if (aborted(shouldAbort)) ok = false;
  if (declaredSize >= 0 && total != static_cast<size_t>(declaredSize))
    ok = false;
  http.end();

  if (!ok || total == 0) {
    free(data);
    return false;
  }
  if (total < capacity) {
    uint8_t* resized = static_cast<uint8_t*>(ps_realloc(data, total));
    if (resized) data = resized;
  }
  output = data;
  outputLength = total;
  LOG.printf("[http] live image held in PSRAM (%lu bytes, not cached)\n",
             static_cast<unsigned long>(total));
  return true;
}

String imageExtension(const String& url) {
  String path = url;
  const int query = path.indexOf('?');
  if (query >= 0) path.remove(query);
  const int dot = path.lastIndexOf('.');
  if (dot < 0) return "";
  String extension = path.substring(dot);
  extension.toLowerCase();
  if (extension == ".png" || extension == ".jpg" ||
      extension == ".jpeg" || extension == ".bmp")
    return extension;
  return "";
}

}  // namespace net_http
