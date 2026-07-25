#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// HTTP client helpers shared by every viewer app. The retry, streaming,
// and cancellation logic previously lived in xkcd-viewer/main.cpp; it's
// generic enough to be useful for any app that fetches JSON or images
// over HTTPS. All entry points take timeouts as parameters so callers
// can enforce their own network budgets.
namespace net_http {

// Optional callback invoked while a transfer is running so the caller
// can cancel it (e.g., a viewer with a physical cancel button, or an
// app with a wall-clock network deadline). Returns true when the
// current operation should abort. Pass nullptr to disable.
using ShouldAbortFn = bool (*)();

// Optional callback for xkcd-viewer-style dynamic timeout clamping:
// takes the raw timeout the download loop would use and returns a
// possibly-shorter value (e.g., min(raw, remainingDeadline)). Pass
// nullptr to use the raw timeout unchanged.
using BoundTimeoutFn = uint32_t (*)(uint32_t raw);

// GET the URL and return the response body as a String. `timeoutMs`
// bounds both connect and read waits; `shouldAbort` and `boundTimeout`
// mirror the xkcd-viewer semantics above. Returns false on any HTTP
// error, cancellation, or empty body.
bool getString(const String& url, String& body, uint32_t timeoutMs,
               ShouldAbortFn shouldAbort = nullptr,
               BoundTimeoutFn boundTimeout = nullptr);

// Stream the URL directly to `destination` on the SD card, using a
// .part temporary + rename. `idleTimeoutMs` bounds the gap between
// successive reads. Refuses to write more than `maxBytes` bytes.
bool downloadToSd(const String& url, const String& destination,
                  uint32_t connectTimeoutMs, uint32_t idleTimeoutMs,
                  size_t maxBytes,
                  ShouldAbortFn shouldAbort = nullptr,
                  BoundTimeoutFn boundTimeout = nullptr);

// Stream the URL into a freshly allocated PSRAM buffer. On success
// `output` points to the buffer (caller frees with free()) and
// `outputLength` holds the byte count. Refuses to allocate more than
// `maxBytes` bytes.
bool downloadToMemory(const String& url, uint8_t*& output,
                      size_t& outputLength, uint32_t connectTimeoutMs,
                      uint32_t idleTimeoutMs, size_t maxBytes,
                      ShouldAbortFn shouldAbort = nullptr,
                      BoundTimeoutFn boundTimeout = nullptr);

// Extract the recognised image extension (".png"/".jpg"/".jpeg"/
// ".bmp") from `url`, ignoring any query string. Returns an empty
// String when no supported extension is present.
String imageExtension(const String& url);

}  // namespace net_http
