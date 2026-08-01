#pragma once

#include <Arduino.h>
#include <IPAddress.h>

class WebServer;

// SD-card Wi-Fi portal shared by the tools/sd-web utility and, later,
// any viewer app that wants to expose its SD card over Wi-Fi for
// operator-side maintenance (uploading fonts, deleting cache dirs,
// pulling logs, etc.).
//
// Runtime flow, once `begin()` has succeeded:
//   1. The ESP32 is running as a soft-AP on `apIp` (default 192.168.1.1),
//      subnet /24, with DHCP handing out `apIp` + 1 onward to clients.
//   2. An HTTP server is listening on `httpPort` (default 80).
//   3. The caller pumps `loop()` from their Arduino loop().
//
// Preconditions the caller must satisfy before begin():
//   * Panel SPI has been finalised (epaper_setup::finalize).
//   * The SD card has been mounted (sd_card::mount).
// The portal does NOT mount the SD itself so an embedding app can share
// its own mount, and so this file never touches epaper/SD headers -
// keeping the header cheap to include from anywhere.
namespace sd_web_portal {

struct Config {
  // Prefix for the auto-generated AP SSID. The full SSID is
  // "<prefix><first 6 hex digits of the AP MAC, no colons>" which for
  // MAC AA:BB:CC:DD:EE:FF and prefix "ReTerminal " yields
  // "ReTerminal AABBCC". Six digits identify one reTerminal well enough
  // within a room of them while still fitting on a small QR code.
  const char* apSsidPrefix = "ReTerminal ";

  // Password for the AP. When null/empty the AP is open (WPA-none),
  // which is what the standalone tool ships with because the QR codes
  // assume no credential.
  const char* apPassword = nullptr;

  // Static IP configuration for the AP. Kept as literals so the QR code
  // payload can be composed from these fields with no round-trip
  // through the softAP object.
  IPAddress apIp{192, 168, 1, 1};
  IPAddress apGateway{192, 168, 1, 1};
  IPAddress apNetmask{255, 255, 255, 0};

  // TCP port for the built-in web server.
  uint16_t httpPort = 80;

  // Maximum simultaneous Wi-Fi clients. ESP32 supports up to 10 STA
  // clients on softAP; four is plenty for a maintenance portal and
  // keeps the DHCP pool small enough to be predictable.
  uint8_t maxConnections = 4;

  // --- Optional photo-upload extension ---------------------------------
  // Set these to enable the browser-side photo-upload page at
  // `/upload-photo`. Leave defaults to keep the portal a plain file
  // browser (which is what the standalone tools/sd-web ships as).
  //
  // The browser resizes and dithers the uploaded image to a 4-bit BMP
  // matching the panel's native palette, then POSTs it to /upload with
  // `parent=<photosDir>` so it lands next to any other photos.
  //
  // Palette names: "gray4" (E1001), "gray16" (E1003), "e6" (E1002/E1004).
  int panelWidth = 0;
  int panelHeight = 0;
  const char* panelPalette = nullptr;
  const char* panelModel = nullptr;    // e.g. "E1001" - shown in the UI
  const char* photosDir = nullptr;     // e.g. "/photos"

  // Path the on-panel URL QR code should point to. Empty/nullptr means
  // "root" (file browser). Set to "/upload-photo" for viewer apps that
  // want the QR to land directly on the photo-upload page.
  const char* urlQrPath = nullptr;

  // Optional cross-portal nav strip HTML fragment injected at the top of
  // every SD portal page (above the "SD Card Portal" header). When the
  // portal is embedded next to the shared Wi-Fi config portal, pass a
  // matching nav bar so the user sees the same tabs on every page.
  const char* navHtml = nullptr;

  // --- Optional thumbnail cache -----------------------------------------
  // When both `thumbnailDir` and `thumbnailGenerator` are set, the portal
  // exposes GET /thumbnail?path=/photos/foo.png. Cache lookups return
  // the pre-rendered BMP under `<thumbnailDir>/<basename>`; a miss calls
  // the generator to build one. On generator failure the cache stores a
  // small placeholder BMP so corrupted photos never retry (or crash).
  //
  // The generator signature is:
  //   bool gen(const char* sourcePath, const char* destPath, int maxDim);
  // It should read `sourcePath`, produce a small (at most `maxDim` on
  // each side) 24bpp BMP at `destPath`, and return true on success. On
  // any decode/OOM error it must return false without leaving a partial
  // file. Runs inside an HTTP handler on the main task, so it should
  // complete in a couple of seconds.
  //
  // Cache is invalidated automatically on upload (UPLOAD_FILE_START) and
  // on /delete-photo, so a re-uploaded file always gets a fresh render.
  using ThumbnailGenerator = bool (*)(const char* sourcePath,
                                      const char* destPath, int maxDim);
  const char* thumbnailDir = nullptr;      // e.g. "/thumb_cache"
  ThumbnailGenerator thumbnailGenerator = nullptr;
  int thumbnailMaxDim = 160;               // longest edge of cached BMP
};

// Build the SSID that begin() would use for the given config, without
// starting the AP. Handy for UI code that wants to display the SSID
// before begin() runs (or for tests). Reads the AP MAC via esp_read_mac.
String buildSsid(const Config& cfg = Config{});

// Format the WPA-none Wi-Fi QR payload for the given SSID. Encodes as
//   WIFI:S:<ssid>;T:nopass;;
// with the SSID characters ; \ , " : escaped per the WPA QR spec. When
// `password` is non-empty, emits T:WPA;P:<password>;; instead.
String wifiQrPayload(const String& ssid, const char* password = nullptr);

// Format the browser QR payload for the given AP IP + port. When `path`
// is non-null and non-empty (e.g. "/upload-photo"), it is appended to
// the URL - handy for viewers that want the QR to open a specific page.
String urlQrPayload(const IPAddress& ip, uint16_t port = 80,
                    const char* path = nullptr);

// Start the AP + HTTP server. Returns true on success. Idempotent
// against duplicate calls (a second call re-applies config).
bool begin(const Config& cfg = Config{});

// Embed mode: install the SD-portal handlers on an existing WebServer
// that another module (typically config_portal) already owns and runs.
// The caller is responsible for starting/pumping the WebServer and for
// AP/DNS setup - this call only registers routes and stores the config.
// Routes registered: /browse, /download, /mkdir, /delete, /upload, and,
// when the photo-uploader is enabled (panelWidth/panelHeight/photosDir
// non-empty), /photo-panel.json, /upload-photo, /exit-portal.
void attachRoutes(::WebServer& server, const Config& cfg);

// Service in-flight HTTP requests. Call every loop() iteration; blocks
// only for the duration of a single request or client-connection tick.
void loop();

// Cheap accessors valid after begin(). Empty string / 0.0.0.0 before.
const String& currentSsid();
IPAddress currentIp();
uint16_t currentPort();

// Set to true once a browser POSTs /exit-portal (the "Reboot to viewer"
// button on the SD portal pages). Firmware polls this from its portal
// loop to trigger the same exit path as the physical arrow buttons.
bool exitRequested();

// Stop the HTTP server and shut down the AP. Not normally needed - the
// portal is a one-shot power-cycle tool - but useful for embedding apps
// that want to run the portal on demand.
void end();

}  // namespace sd_web_portal
