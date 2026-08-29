#include "calendar_http.h"

#include <Stream.h>

#include <algorithm>
#include <limits>

namespace calendar_http {
namespace {

class LimitedStringStream final : public Stream {
 public:
  LimitedStringStream(String& destination, size_t maximumBytes)
      : destination_(destination), maximumBytes_(maximumBytes) {}

  size_t write(uint8_t value) override { return write(&value, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    if (size > maximumBytes_ - destination_.length()) {
      limitExceeded_ = true;
      setWriteError();
      return 0;
    }
    if (!destination_.concat(reinterpret_cast<const char*>(buffer), size)) {
      allocationFailed_ = true;
      setWriteError();
      return 0;
    }
    return size;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

  bool limitExceeded() const { return limitExceeded_; }
  bool allocationFailed() const { return allocationFailed_; }

 private:
  String& destination_;
  size_t maximumBytes_;
  bool limitExceeded_ = false;
  bool allocationFailed_ = false;
};

}  // namespace

bool readBody(HTTPClient& http, size_t maximumBytes, uint32_t idleTimeoutMs,
              String& body, String& failureReason) {
  body = "";
  failureReason = "";
  const int declaredSize = http.getSize();
  if (declaredSize > static_cast<int>(maximumBytes)) {
    failureReason = "HTTP response exceeds the configured size limit";
    return false;
  }

  const size_t initialCapacity =
      declaredSize > 0
          ? static_cast<size_t>(declaredSize)
          : std::min(maximumBytes, static_cast<size_t>(16U * 1024U));
  if (initialCapacity > 0 && !body.reserve(initialCapacity)) {
    failureReason = "Could not allocate memory for the HTTP response";
    return false;
  }

  http.setTimeout(static_cast<uint16_t>(
      std::min<uint32_t>(idleTimeoutMs,
                         std::numeric_limits<uint16_t>::max())));
  LimitedStringStream destination(body, maximumBytes);
  const int written = http.writeToStream(&destination);
  if (destination.limitExceeded()) {
    failureReason = "HTTP response exceeds the configured size limit";
    return false;
  }
  if (destination.allocationFailed()) {
    failureReason = "Could not grow the HTTP response buffer";
    return false;
  }
  if (written < 0) {
    const String detail = HTTPClient::errorToString(written);
    failureReason = detail.isEmpty()
                        ? "Could not read the HTTP response"
                        : "Could not read the HTTP response: " + detail;
    return false;
  }
  if (static_cast<size_t>(written) != body.length()) {
    failureReason = "HTTP response ended unexpectedly";
    return false;
  }
  return true;
}

}  // namespace calendar_http
