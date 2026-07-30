#include "sd_web_portal.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <FS.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "app_logger.h"
#include "sd_card.h"

// SD Wi-Fi portal implementation.
//
// Uses the sync WebServer.h built into Arduino-ESP32. Simpler than an
// async server and sufficient for a maintenance tool driven by one
// operator at a time - no fan-in of concurrent requests, no need for
// upload chunks to be streamed while other handlers run.
//
// The HTML is deliberately minimal / vanilla (no framework, no JS) so
// there is no build step and the whole portal fits in flash without any
// LittleFS/SPIFFS partition.

namespace sd_web_portal {

namespace {

// One WebServer instance per portal. The library only supports one
// active portal per process; that matches the intended use (a single
// tool app, or a single opt-in maintenance mode in a viewer app).
WebServer* g_server = nullptr;
DNSServer* g_dns = nullptr;

Config g_config;
String g_ssid;
String g_url;
bool g_running = false;

// Upload state. Managed inside the /upload handler; kept as file-scope
// so start/write/end callbacks (which are separate invocations of the
// upload callback) can share a File handle.
File g_uploadFile;
String g_uploadTargetPath;
bool g_uploadOk = false;

// ----- helpers -----

String macToHexSuffix(const uint8_t mac[6]) {
  char buf[13];
  snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[0], mac[1], mac[2]);
  return String(buf);
}

String formatMac(const uint8_t mac[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

// Escape a raw string for safe insertion into an HTML text node or
// attribute value. Not a general-purpose HTML sanitizer - just enough
// to keep file names with < > & " ' from breaking the page.
String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c; break;
    }
  }
  return out;
}

// Percent-encode a raw string for embedding into a URL query value.
// Encodes anything that isn't unreserved per RFC 3986, so spaces and
// UTF-8 file names round-trip correctly through the query string.
String urlEncode(const String& in) {
  static const char kHex[] = "0123456789ABCDEF";
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(in[i]);
    const bool unreserved =
        (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.' ||
        c == '~';
    if (unreserved) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    }
  }
  return out;
}

// Normalise a caller-provided path into an absolute SD path with no
// ".." segments, no double slashes, and no trailing slash (except for
// the root "/"). Returns "" for anything unrepresentable.
String normalisePath(const String& raw) {
  // Enforce absolute paths only - refuse anything else defensively.
  if (raw.length() == 0 || raw[0] != '/') return String();
  // Split on '/' and rebuild, dropping "" and "." and popping on "..".
  String out;
  out.reserve(raw.length());
  int i = 0;
  while (i < static_cast<int>(raw.length())) {
    // Skip the leading '/'.
    while (i < static_cast<int>(raw.length()) && raw[i] == '/') ++i;
    if (i >= static_cast<int>(raw.length())) break;
    int j = i;
    while (j < static_cast<int>(raw.length()) && raw[j] != '/') ++j;
    const String seg = raw.substring(i, j);
    i = j;
    if (seg == "" || seg == ".") continue;
    if (seg == "..") {
      // Pop the last segment from `out`.
      const int slash = out.lastIndexOf('/');
      if (slash >= 0) out.remove(slash);
      continue;
    }
    // Reject NUL bytes and control chars; FAT/exFAT can't hold them.
    for (size_t k = 0; k < seg.length(); ++k) {
      if (static_cast<uint8_t>(seg[k]) < 0x20) return String();
    }
    out += '/';
    out += seg;
  }
  if (out.length() == 0) return String("/");
  return out;
}

String parentOf(const String& path) {
  const int slash = path.lastIndexOf('/');
  if (slash <= 0) return String("/");
  return path.substring(0, slash);
}

String joinPath(const String& dir, const String& name) {
  if (dir.length() == 0 || dir == "/") return String("/") + name;
  return dir + "/" + name;
}

String humanBytes(size_t bytes) {
  char buf[24];
  if (bytes < 1024) {
    snprintf(buf, sizeof(buf), "%u B", static_cast<unsigned>(bytes));
  } else if (bytes < 1024ULL * 1024ULL) {
    snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
  } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
    snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
  } else {
    snprintf(buf, sizeof(buf), "%.2f GB",
             bytes / (1024.0 * 1024.0 * 1024.0));
  }
  return String(buf);
}

String formatTime(time_t t) {
  if (t <= 0) return String("-");
  struct tm tmv;
  // File::getLastWrite() returns time_t. We treat it as UTC for display;
  // the tool doesn't run NTP so this is the FAT-encoded local time from
  // whichever host wrote the file.
  gmtime_r(&t, &tmv);
  char buf[24];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
           tmv.tm_min);
  return String(buf);
}

// ----- HTML rendering -----

void sendPageHeader(const String& title, const String& breadcrumbHtml) {
  // Use chunked transfer encoding: setContentLength(CONTENT_LENGTH_UNKNOWN)
  // + send(code, type, "") tells WebServer to omit Content-Length and
  // use "Transfer-Encoding: chunked" instead. If we send() with a
  // non-empty body, Content-Length is set to that body's length and the
  // browser stops reading after it - dropping every subsequent
  // sendContent() chunk on the floor.
  g_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  g_server->send(200, "text/html; charset=utf-8", "");

  String html;
  html.reserve(2048);
  html += F(
      "<!doctype html>\n"
      "<html><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>");
  html += htmlEscape(title);
  html += F(
      "</title>"
      "<style>"
      "*,*::before,*::after{box-sizing:border-box}"
      "body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:0;background:#f4f4f4;color:#222;font-size:16px;line-height:1.4;-webkit-text-size-adjust:100%}"
      "header{background:#111;color:#fff;padding:1rem;position:sticky;top:0;z-index:10}"
      "header h1{margin:0;font-size:1.2rem}"
      "header .crumb{font-size:.95rem;color:#ccc;margin-top:.5rem;word-break:break-all;line-height:1.5}"
      "header .crumb a{color:#8cf;text-decoration:none;padding:.15rem .1rem}"
      "main{padding:1rem;max-width:900px;margin:auto}"
      "section{background:#fff;border-radius:8px;padding:1rem;margin-bottom:1rem;box-shadow:0 1px 3px rgba(0,0,0,.08)}"
      "section h2{margin:.2rem 0 .75rem 0;font-size:1.05rem;color:#555}"
      "table{width:100%;border-collapse:collapse;font-size:1rem}"
      "th,td{text-align:left;padding:.6rem .35rem;border-bottom:1px solid #eee;vertical-align:middle}"
      "th{color:#666;font-weight:600;font-size:.9rem;text-transform:uppercase;letter-spacing:.02em}"
      "td.size,td.mtime,th.size,th.mtime{white-space:nowrap;color:#666;font-variant-numeric:tabular-nums}"
      "td.actions{text-align:right;white-space:nowrap}"
      "a.name{color:#036;text-decoration:none;font-weight:500;display:inline-block;padding:.2rem 0;word-break:break-all}"
      "a.name:hover{text-decoration:underline}"
      ".dir a.name::before{content:\"\\1F4C1 \";}"
      ".file a.name::before{content:\"\\1F4C4 \";}"
      "button,.btn{background:#036;color:#fff;border:0;padding:.6rem 1rem;border-radius:6px;cursor:pointer;font-size:1rem;font-family:inherit;text-decoration:none;display:inline-block;min-height:44px;line-height:1.2}"
      "button:hover,.btn:hover{background:#048}"
      "button.danger{background:#a22}"
      "button.danger:hover{background:#c33}"
      "form.inline{display:inline}"
      "form.row{display:flex;gap:.6rem;margin-top:.5rem;flex-wrap:wrap;align-items:center}"
      "form.row input[type=text],form.row input[type=file]{flex:1 1 200px;min-width:0;width:100%;padding:.6rem;border:1px solid #ccc;border-radius:6px;font-size:1rem;font-family:inherit;background:#fff;height:2.75rem;line-height:1.2}"
      "form.row input[type=text]:focus,form.row input[type=file]:focus{outline:none;border-color:#036;box-shadow:0 0 0 3px rgba(0,102,153,.2)}"
      "form.row button{flex:0 0 auto}"
      ".empty{color:#888;font-style:italic;padding:.75rem 0}"
      ".note{color:#666;font-size:.9rem;margin-top:.6rem;line-height:1.5}"
      ".flash{background:#efe;border:1px solid #cec;color:#252;padding:.75rem;border-radius:6px;margin-bottom:1rem;font-size:1rem}"
      ".flash.err{background:#fee;border-color:#ecc;color:#722}"
      "@media (max-width:600px){"
      "main{padding:.75rem}"
      "section{padding:.85rem;border-radius:6px}"
      "th.mtime,td.mtime{display:none}"
      "th.size,td.size{font-size:.9rem}"
      "th,td{padding:.7rem .3rem}"
      "form.row{flex-direction:column;align-items:stretch}"
      "form.row input[type=text],form.row input[type=file]{flex:0 0 auto;width:100%}"
      "form.row button{width:100%}"
      "td.actions button{width:auto}"
      "}"
      "@media (min-width:601px){"
      "header{padding:1rem 1.25rem}"
      "}"
      "</style>"
      "</head><body>"
      "<header><h1>SD Card Portal</h1>"
      "<div class=\"crumb\">");
  html += breadcrumbHtml;
  html += F("</div></header><main>");
  g_server->sendContent(html);
}

void sendPageChunk(const String& chunk) {
  g_server->sendContent(chunk);
}

void sendPageFooter() {
  g_server->sendContent(F("</main></body></html>"));
  g_server->sendContent("");
}

String breadcrumb(const String& path) {
  String html = F("<a href=\"/\">/</a>");
  if (path == "/" || path.length() == 0) return html;
  String acc;
  int i = 0;
  while (i < static_cast<int>(path.length())) {
    while (i < static_cast<int>(path.length()) && path[i] == '/') ++i;
    if (i >= static_cast<int>(path.length())) break;
    int j = i;
    while (j < static_cast<int>(path.length()) && path[j] != '/') ++j;
    const String seg = path.substring(i, j);
    acc += '/';
    acc += seg;
    html += F(" &rsaquo; <a href=\"/browse?path=");
    html += urlEncode(acc);
    html += F("\">");
    html += htmlEscape(seg);
    html += F("</a>");
    i = j;
  }
  return html;
}

// Extract an optional flash message from the query string.  We use
// short codes so redirects don't need to embed long UTF-8 text; the
// human-readable string lives here.
String flashHtml() {
  if (!g_server->hasArg("m")) return String();
  const String code = g_server->arg("m");
  const char* text = nullptr;
  const char* cls = "flash";
  if (code == "up_ok")   text = "Upload complete.";
  else if (code == "up_err")  { text = "Upload failed."; cls = "flash err"; }
  else if (code == "mk_ok")   text = "Folder created.";
  else if (code == "mk_err")  { text = "Could not create folder."; cls = "flash err"; }
  else if (code == "del_ok")  text = "Deleted.";
  else if (code == "del_err") { text = "Delete failed."; cls = "flash err"; }
  else if (code == "bad")     { text = "Invalid request."; cls = "flash err"; }
  if (!text) return String();
  String html;
  html += F("<div class=\"");
  html += cls;
  html += F("\">");
  html += htmlEscape(String(text));
  html += F("</div>");
  return html;
}

// ----- request handlers -----

void handleRoot() {
  // Redirect to /browse?path=/
  g_server->sendHeader("Location", "/browse?path=%2F");
  g_server->send(302, "text/plain", "");
}

void handleBrowse() {
  String path = g_server->hasArg("path") ? g_server->arg("path") : String("/");
  path = normalisePath(path);
  if (path.length() == 0) {
    g_server->sendHeader("Location", "/browse?path=%2F&m=bad");
    g_server->send(302, "text/plain", "");
    return;
  }

  File dir = SD.open(path);
  if (!dir) {
    g_server->sendHeader("Location", "/browse?path=%2F&m=bad");
    g_server->send(302, "text/plain", "");
    return;
  }
  if (!dir.isDirectory()) {
    // Treat as a download request instead.
    dir.close();
    g_server->sendHeader("Location",
                        String("/download?path=") + urlEncode(path));
    g_server->send(302, "text/plain", "");
    return;
  }

  sendPageHeader(String("SD: ") + path, breadcrumb(path));

  String prelude = flashHtml();
  prelude += F("<section><h2>Contents</h2>");
  sendPageChunk(prelude);

  // Two-pass render: collect entries into two arrays so directories can
  // sort ahead of files. Kept small because each entry only holds the
  // strings we need for output.
  struct Entry {
    String name;
    bool isDir;
    size_t size;
    time_t mtime;
  };
  const size_t kMaxEntries = 512;
  Entry* entries = new Entry[kMaxEntries];
  size_t count = 0;
  File child = dir.openNextFile();
  while (child && count < kMaxEntries) {
    entries[count].name = String(child.name());
    // File::name() returns the *full* path on Arduino SD; extract the
    // basename so we can join with `path` cleanly for links.
    const int slash = entries[count].name.lastIndexOf('/');
    if (slash >= 0) entries[count].name = entries[count].name.substring(slash + 1);
    entries[count].isDir = child.isDirectory();
    entries[count].size = child.size();
    entries[count].mtime = child.getLastWrite();
    child.close();
    ++count;
    child = dir.openNextFile();
  }
  if (child) child.close();
  dir.close();

  // Sort: directories first, then case-insensitive name.
  for (size_t i = 1; i < count; ++i) {
    Entry key = entries[i];
    size_t j = i;
    while (j > 0) {
      const Entry& prev = entries[j - 1];
      bool prevFirst;
      if (prev.isDir != key.isDir)
        prevFirst = prev.isDir;
      else
        prevFirst =
            strcasecmp(prev.name.c_str(), key.name.c_str()) <= 0;
      if (prevFirst) break;
      entries[j] = entries[j - 1];
      --j;
    }
    entries[j] = key;
  }

  if (count == 0) {
    sendPageChunk(F("<div class=\"empty\">(empty folder)</div>"));
  } else {
    sendPageChunk(F(
        "<table><thead><tr><th>Name</th><th class=\"size\">Size</th>"
        "<th class=\"mtime\">Modified</th><th></th></tr></thead><tbody>"));

    for (size_t i = 0; i < count; ++i) {
      const Entry& e = entries[i];
      const String full = joinPath(path, e.name);
      String row;
      row.reserve(400);
      row += e.isDir ? F("<tr class=\"dir\"><td>") : F("<tr class=\"file\"><td>");
      row += F("<a class=\"name\" href=\"");
      row += e.isDir ? F("/browse?path=") : F("/download?path=");
      row += urlEncode(full);
      row += F("\">");
      row += htmlEscape(e.name);
      row += F("</a></td><td class=\"size\">");
      row += e.isDir ? String("-") : humanBytes(e.size);
      row += F("</td><td class=\"mtime\">");
      row += formatTime(e.mtime);
      row += F("</td><td class=\"actions\">");
      row += F("<form class=\"inline\" method=\"post\" action=\"/delete\" onsubmit=\"return confirm('Delete ");
      row += htmlEscape(e.name);
      row += F("?');\">");
      row += F("<input type=\"hidden\" name=\"path\" value=\"");
      row += htmlEscape(full);
      row += F("\">");
      row += F("<input type=\"hidden\" name=\"parent\" value=\"");
      row += htmlEscape(path);
      row += F("\">");
      row += F("<button class=\"danger\">Delete</button></form>");
      row += F("</td></tr>");
      sendPageChunk(row);
    }
    sendPageChunk(F("</tbody></table>"));
  }
  delete[] entries;

  // Upload + mkdir forms.
  String actions;
  actions.reserve(1024);
  actions += F("</section><section><h2>Create folder</h2>");
  actions += F("<form class=\"row\" method=\"post\" action=\"/mkdir\">");
  actions += F("<input type=\"hidden\" name=\"parent\" value=\"");
  actions += htmlEscape(path);
  actions += F("\">");
  actions += F("<input type=\"text\" name=\"name\" placeholder=\"folder name\" required maxlength=\"200\">");
  actions += F("<button>Create</button></form></section>");

  actions += F("<section><h2>Upload file</h2>");
  actions += F("<form class=\"row\" method=\"post\" action=\"/upload?parent=");
  actions += urlEncode(path);
  actions += F("\" enctype=\"multipart/form-data\">");
  actions += F("<input type=\"file\" name=\"file\" required>");
  actions += F("<button>Upload</button></form>");
  actions += F("<div class=\"note\">Uploads are streamed directly to the card. Existing files will be overwritten.</div>");
  actions += F("</section>");
  sendPageChunk(actions);

  sendPageFooter();
}

void handleDownload() {
  String path = g_server->hasArg("path") ? g_server->arg("path") : String();
  path = normalisePath(path);
  if (path.length() == 0) {
    g_server->send(400, "text/plain", "bad path");
    return;
  }
  File file = SD.open(path);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    g_server->send(404, "text/plain", "not found");
    return;
  }
  // Basename for the download prompt.
  int slash = path.lastIndexOf('/');
  const String base = slash >= 0 ? path.substring(slash + 1) : path;
  g_server->sendHeader("Content-Disposition",
                       String("attachment; filename=\"") + base + "\"");
  g_server->streamFile(file, "application/octet-stream");
  file.close();
}

void redirectToBrowse(const String& path, const char* flash) {
  String target = "/browse?path=" + urlEncode(path);
  if (flash && flash[0]) {
    target += "&m=";
    target += flash;
  }
  g_server->sendHeader("Location", target);
  g_server->send(302, "text/plain", "");
}

void handleMkdir() {
  if (!g_server->hasArg("parent") || !g_server->hasArg("name")) {
    g_server->sendHeader("Location", "/browse?path=%2F&m=bad");
    g_server->send(302, "text/plain", "");
    return;
  }
  String parent = normalisePath(g_server->arg("parent"));
  String name = g_server->arg("name");
  name.trim();
  if (parent.length() == 0 || name.length() == 0 ||
      name.indexOf('/') >= 0 || name == "." || name == "..") {
    redirectToBrowse(parent.length() == 0 ? String("/") : parent, "bad");
    return;
  }
  const String target = joinPath(parent, name);
  const bool ok = SD.mkdir(target);
  LOG.printf("[sd-web] mkdir %s -> %s\n", target.c_str(), ok ? "ok" : "fail");
  redirectToBrowse(parent, ok ? "mk_ok" : "mk_err");
}

// Recursive rmdir: attempt to remove a directory and its contents. SD
// library's rmdir refuses non-empty dirs, so we descend ourselves.
bool removeRecursive(const String& path) {
  File node = SD.open(path);
  if (!node) return false;
  if (!node.isDirectory()) {
    node.close();
    return SD.remove(path);
  }
  // Collect entry names first (can't reliably mutate while iterating).
  const size_t kMaxChildren = 256;
  String* names = new String[kMaxChildren];
  bool* isDir = new bool[kMaxChildren];
  size_t n = 0;
  File child = node.openNextFile();
  while (child && n < kMaxChildren) {
    String cname = String(child.name());
    const int slash = cname.lastIndexOf('/');
    if (slash >= 0) cname = cname.substring(slash + 1);
    names[n] = cname;
    isDir[n] = child.isDirectory();
    ++n;
    child.close();
    child = node.openNextFile();
  }
  if (child) child.close();
  node.close();

  bool ok = true;
  for (size_t i = 0; i < n; ++i) {
    const String childPath = joinPath(path, names[i]);
    if (isDir[i]) {
      ok = removeRecursive(childPath) && ok;
    } else {
      ok = SD.remove(childPath) && ok;
    }
  }
  delete[] names;
  delete[] isDir;
  if (!ok) return false;
  return SD.rmdir(path);
}

void handleDelete() {
  if (!g_server->hasArg("path")) {
    g_server->sendHeader("Location", "/browse?path=%2F&m=bad");
    g_server->send(302, "text/plain", "");
    return;
  }
  String target = normalisePath(g_server->arg("path"));
  const String parent = g_server->hasArg("parent")
                            ? normalisePath(g_server->arg("parent"))
                            : parentOf(target);
  if (target.length() == 0 || target == "/") {
    redirectToBrowse(parent.length() ? parent : String("/"), "bad");
    return;
  }
  File node = SD.open(target);
  if (!node) {
    redirectToBrowse(parent.length() ? parent : String("/"), "del_err");
    return;
  }
  const bool isDir = node.isDirectory();
  node.close();
  bool ok;
  if (isDir) {
    ok = removeRecursive(target);
  } else {
    ok = SD.remove(target);
  }
  LOG.printf("[sd-web] delete %s (dir=%d) -> %s\n", target.c_str(),
             isDir ? 1 : 0, ok ? "ok" : "fail");
  redirectToBrowse(parent.length() ? parent : String("/"),
                   ok ? "del_ok" : "del_err");
}

// Upload lifecycle handler. Called multiple times per upload:
//   UPLOAD_FILE_START  -> open destination file
//   UPLOAD_FILE_WRITE  -> append chunk
//   UPLOAD_FILE_END    -> close file, mark success
//   UPLOAD_FILE_ABORTED-> discard partial file
void handleUploadStream() {
  HTTPUpload& upload = g_server->upload();
  switch (upload.status) {
    case UPLOAD_FILE_START: {
      const String parent = g_server->hasArg("parent")
                                ? normalisePath(g_server->arg("parent"))
                                : String("/");
      String name = upload.filename;
      const int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      // Reject filenames with control chars or empty basename.
      bool bad = name.length() == 0;
      for (size_t i = 0; !bad && i < name.length(); ++i) {
        if (static_cast<uint8_t>(name[i]) < 0x20) bad = true;
      }
      if (bad || parent.length() == 0) {
        g_uploadOk = false;
        g_uploadTargetPath = String();
        return;
      }
      g_uploadTargetPath = joinPath(parent, name);
      // Overwrite by removing first; SD FILE_WRITE opens truncating,
      // but be explicit so a failed open doesn't leave a stale file.
      SD.remove(g_uploadTargetPath);
      g_uploadFile = SD.open(g_uploadTargetPath, FILE_WRITE);
      g_uploadOk = static_cast<bool>(g_uploadFile);
      LOG.printf("[sd-web] upload start %s -> %s\n",
                 g_uploadTargetPath.c_str(), g_uploadOk ? "ok" : "open-fail");
      break;
    }
    case UPLOAD_FILE_WRITE: {
      if (g_uploadOk && g_uploadFile) {
        const size_t written =
            g_uploadFile.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
          g_uploadOk = false;
          LOG.printf("[sd-web] upload short write on %s\n",
                     g_uploadTargetPath.c_str());
        }
      }
      break;
    }
    case UPLOAD_FILE_END: {
      if (g_uploadFile) g_uploadFile.close();
      LOG.printf("[sd-web] upload end %s (%u bytes) -> %s\n",
                 g_uploadTargetPath.c_str(),
                 static_cast<unsigned>(upload.totalSize),
                 g_uploadOk ? "ok" : "fail");
      break;
    }
    case UPLOAD_FILE_ABORTED: {
      if (g_uploadFile) g_uploadFile.close();
      if (g_uploadTargetPath.length()) SD.remove(g_uploadTargetPath);
      g_uploadOk = false;
      LOG.printf("[sd-web] upload aborted %s\n", g_uploadTargetPath.c_str());
      break;
    }
    default:
      break;
  }
}

void handleUploadDone() {
  const String parent = g_server->hasArg("parent")
                            ? normalisePath(g_server->arg("parent"))
                            : String("/");
  redirectToBrowse(parent.length() ? parent : String("/"),
                   g_uploadOk ? "up_ok" : "up_err");
}

// Apple's "Success" body. If the phone's Captive Network Assistant
// gets exactly this from http://captive.apple.com/hotspot-detect.html
// (or one of the sibling probe URLs), it decides the network has
// internet and joins silently without popping the captive portal
// sheet. See support.apple.com/en-us/102554.
const char kAppleSuccess[] PROGMEM =
    "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
    "<BODY>Success</BODY></HTML>";

void handleAppleProbe() {
  g_server->send(200, "text/html", FPSTR(kAppleSuccess));
}

// Everything else - including any URL the user types into Safari
// while joined to our AP - gets redirected back to the portal root.
// Combined with the DNS hijack below, this means typing "google.com"
// (or any hostname) opens the SD card browser.
void handleNotFound() {
  // Skip the redirect for Apple's probes; they're already handled by
  // explicit routes, but a very old iOS might use an unknown path
  // under captive.apple.com. Returning "Success" for those keeps CNA
  // dismissed.
  const String& host = g_server->hostHeader();
  if (host.indexOf("apple.com") >= 0 ||
      host.indexOf("apple.")   == 0) {
    g_server->send(200, "text/html", FPSTR(kAppleSuccess));
    return;
  }
  String target = "http://";
  target += g_config.apIp.toString();
  if (g_config.httpPort != 80) {
    target += ':';
    target += String(g_config.httpPort);
  }
  target += '/';
  g_server->sendHeader("Location", target, true);
  g_server->send(302, "text/plain", "");
}

}  // namespace

// ----- public API -----

String buildSsid(const Config& cfg) {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  String s = cfg.apSsidPrefix ? String(cfg.apSsidPrefix) : String();
  s += macToHexSuffix(mac);
  return s;
}

String wifiQrPayload(const String& ssid, const char* password) {
  // Escape ';' ':' ',' '\' '"' per the informal WPA QR spec.
  auto esc = [](const String& in) {
    String out;
    out.reserve(in.length() + 4);
    for (size_t i = 0; i < in.length(); ++i) {
      const char c = in[i];
      if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
        out += '\\';
      }
      out += c;
    }
    return out;
  };
  String payload = "WIFI:S:";
  payload += esc(ssid);
  if (password && password[0]) {
    payload += ";T:WPA;P:";
    payload += esc(String(password));
    payload += ";;";
  } else {
    payload += ";T:nopass;;";
  }
  return payload;
}

String urlQrPayload(const IPAddress& ip, uint16_t port) {
  String url = "http://";
  url += ip.toString();
  if (port != 80) {
    url += ':';
    url += String(port);
  }
  url += '/';
  return url;
}

bool begin(const Config& cfg) {
  g_config = cfg;

  // Bring the radio into AP mode with the requested static IP.
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAPConfig(cfg.apIp, cfg.apGateway, cfg.apNetmask)) {
    LOG.println("[sd-web] softAPConfig failed");
    return false;
  }
  g_ssid = buildSsid(cfg);
  const char* pass = (cfg.apPassword && cfg.apPassword[0]) ? cfg.apPassword : nullptr;
  // channel 1, not hidden, max_connection = cfg.maxConnections.
  if (!WiFi.softAP(g_ssid.c_str(), pass, 1, 0, cfg.maxConnections)) {
    LOG.println("[sd-web] softAP failed");
    return false;
  }
  g_url = urlQrPayload(cfg.apIp, cfg.httpPort);
  LOG.printf("[sd-web] AP up: ssid=\"%s\" ip=%s\n", g_ssid.c_str(),
             cfg.apIp.toString().c_str());

  if (g_server) {
    g_server->stop();
    delete g_server;
    g_server = nullptr;
  }
  g_server = new WebServer(cfg.httpPort);
  g_server->on("/", handleRoot);
  g_server->on("/browse", handleBrowse);
  g_server->on("/download", handleDownload);
  g_server->on("/mkdir", HTTP_POST, handleMkdir);
  g_server->on("/delete", HTTP_POST, handleDelete);
  // Upload uses two callbacks: the final response, and the streaming
  // handler that receives the multipart chunks.
  g_server->on("/upload", HTTP_POST, handleUploadDone, handleUploadStream);

  // Captive-portal probes. Apple's probes get the exact "Success"
  // body so the Captive Network Assistant dismisses itself. Android
  // and Windows probes get their expected 204/NCSI responses so
  // those OSes also join silently.
  g_server->on("/hotspot-detect.html",        handleAppleProbe);
  g_server->on("/library/test/success.html",  handleAppleProbe);
  g_server->on("/generate_204", []() { g_server->send(204); });
  g_server->on("/gen_204",      []() { g_server->send(204); });
  g_server->on("/ncsi.txt", []() {
    g_server->send(200, "text/plain", "Microsoft NCSI");
  });
  g_server->on("/connecttest.txt", []() {
    g_server->send(200, "text/plain", "Microsoft Connect Test");
  });

  g_server->onNotFound(handleNotFound);
  g_server->begin();

  // DNS hijack: answer every A query with our own IP. Combined with
  // the catch-all handler, typing any hostname in the browser opens
  // the portal, and Apple's probe URLs also resolve to us.
  if (g_dns) { g_dns->stop(); delete g_dns; g_dns = nullptr; }
  g_dns = new DNSServer();
  g_dns->setErrorReplyCode(DNSReplyCode::NoError);
  if (!g_dns->start(53, "*", cfg.apIp)) {
    LOG.println("[sd-web] DNS server failed to start (portal still works via IP)");
    delete g_dns;
    g_dns = nullptr;
  }

  g_running = true;
  LOG.printf("[sd-web] http://%s:%u/\n",
             cfg.apIp.toString().c_str(), cfg.httpPort);
  return true;
}

void loop() {
  if (g_running && g_server) g_server->handleClient();
  if (g_running && g_dns) g_dns->processNextRequest();
}

const String& currentSsid() { return g_ssid; }
IPAddress currentIp() { return g_running ? g_config.apIp : IPAddress(); }
uint16_t currentPort() { return g_config.httpPort; }

void end() {
  if (g_dns) {
    g_dns->stop();
    delete g_dns;
    g_dns = nullptr;
  }
  if (g_server) {
    g_server->stop();
    delete g_server;
    g_server = nullptr;
  }
  if (g_running) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    g_running = false;
  }
}

}  // namespace sd_web_portal
