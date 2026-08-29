#include "google_credentials_portal.h"

#include <WebServer.h>

#include "config.h"
#include "config_portal.h"
#include "google_credentials.h"

namespace google_credentials_portal {
namespace {

WebServer* g_server = nullptr;
String g_pageStart;
String g_uploadBody;
String g_uploadError;
bool g_uploadComplete = false;
bool g_uploadAccepted = false;

String jsonEscape(const String& value) {
  String result;
  result.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '"' || c == '\\') result += '\\';
    if (c == '\n') {
      result += "\\n";
    } else if (c == '\r') {
      result += "\\r";
    } else {
      result += c;
    }
  }
  return result;
}

void scrubUpload() {
  for (size_t i = 0; i < g_uploadBody.length(); ++i) {
    g_uploadBody.setCharAt(i, '\0');
  }
  g_uploadBody = "";
}

void noStore() {
  g_server->sendHeader("Cache-Control", "no-store, max-age=0");
  g_server->sendHeader("Pragma", "no-cache");
}

void sendStatus() {
  noStore();
  const String email = google_credentials::configuredEmail();
  String body = "{\"configured\":";
  body += email.isEmpty() ? "false" : "true";
  body += ",\"email\":\"";
  body += jsonEscape(email);
  body += "\"}";
  g_server->send(200, "application/json", body);
}

void sendPage() {
  noStore();
  String page;
  page.reserve(g_pageStart.length() + 4600);
  page = g_pageStart;
  page += F("<section class=\"card\"><h2>Google service account</h2>"
            "<p class=\"help\">Upload the IAM JSON key downloaded from Google Cloud. "
            "The file is parsed in memory; only the required fields are stored in device NVS. "
            "The private key is never written to SD, returned by this page, or printed to logs.</p>"
            "<label for=\"file\">IAM JSON key</label>"
            "<input id=\"file\" type=\"file\" accept=\"application/json,.json\">"
            "<button id=\"upload\" type=\"button\">Upload credential</button>"
            "<div class=\"msg\" id=\"status\" aria-live=\"polite\"></div></section>"
            "<section class=\"card\"><h2>Calendar access</h2><p class=\"help\">Share each calendar "
            "with the service account email shown below, or configure Workspace domain-wide "
            "delegation and set the delegated user in Settings.</p><p id=\"identity\">Checking...</p>"
            "<button class=\"danger\" id=\"clear\" type=\"button\">Remove credential</button></section>"
            "<section class=\"card\"><h2>Return to calendar</h2>"
            "<p class=\"help\">Restart the device and return to Calendar Viewer.</p>"
            "<button class=\"secondary\" id=\"reboot\" type=\"button\">Reboot to viewer</button></section>"
            "<script>const s=document.getElementById('status'),i=document.getElementById('identity');"
            "function msg(t,ok=false){s.className='msg '+(ok?'ok':'err');s.textContent=t}"
            "async function refresh(){const r=await fetch('/google-credentials.json',{cache:'no-store'});"
            "const j=await r.json();i.textContent=j.configured?('Configured as '+j.email):'Not configured';}"
            "document.getElementById('upload').onclick=async()=>{const f=document.getElementById('file').files[0];"
            "if(!f){msg('Choose a JSON file first.');return}if(f.size>8192){msg('File is too large.');return}"
            "const d=new FormData();d.append('credential',f,f.name);s.className='msg';s.textContent='Uploading...';"
            "const r=await fetch('/google-credentials',{method:'POST',body:d});const j=await r.json();"
            "msg(j.ok?'Credential saved.':j.error,j.ok);await refresh()};"
            "document.getElementById('clear').onclick=async()=>{if(!confirm('Remove the stored Google credential?'))return;"
            "const r=await fetch('/google-credentials/clear',{method:'POST'});const j=await r.json();"
            "msg(j.ok?'Credential removed.':j.error,j.ok);await refresh()};"
            "document.getElementById('reboot').onclick=async()=>{const b=document.getElementById('reboot');"
            "b.disabled=true;b.textContent='Rebooting...';try{await fetch('/reboot',{method:'POST'})}catch(e){}};"
            "refresh();"
            "</script></main></body></html>");
  g_server->send(200, "text/html; charset=utf-8", page);
}

void handleUploadStream() {
  HTTPUpload& upload = g_server->upload();
  switch (upload.status) {
    case UPLOAD_FILE_START:
      scrubUpload();
      g_uploadError = "";
      g_uploadComplete = false;
      g_uploadAccepted = true;
      g_uploadBody.reserve(
          static_cast<unsigned int>(
              min(static_cast<size_t>(upload.totalSize),
                  config::MAX_GOOGLE_CREDENTIAL_BYTES)));
      break;
    case UPLOAD_FILE_WRITE:
      if (!g_uploadAccepted) break;
      if (g_uploadBody.length() + upload.currentSize >
          config::MAX_GOOGLE_CREDENTIAL_BYTES) {
        g_uploadAccepted = false;
        g_uploadError = "Credential file exceeds 8 KiB";
        scrubUpload();
        break;
      }
      g_uploadBody.concat(
          reinterpret_cast<const char*>(upload.buf), upload.currentSize);
      break;
    case UPLOAD_FILE_END:
      if (g_uploadAccepted) {
        g_uploadAccepted =
            google_credentials::storeJson(g_uploadBody, g_uploadError);
      }
      scrubUpload();
      g_uploadComplete = true;
      break;
    case UPLOAD_FILE_ABORTED:
      g_uploadAccepted = false;
      g_uploadComplete = true;
      g_uploadError = "Upload was aborted";
      scrubUpload();
      break;
    default:
      break;
  }
}

void handleUploadDone() {
  noStore();
  if (!g_uploadComplete) {
    g_server->send(400, "application/json",
                   "{\"ok\":false,\"error\":\"No credential file received\"}");
    return;
  }
  if (!g_uploadAccepted) {
    String body = "{\"ok\":false,\"error\":\"";
    body += jsonEscape(g_uploadError);
    body += "\"}";
    g_server->send(400, "application/json", body);
    return;
  }
  g_server->send(200, "application/json", "{\"ok\":true}");
}

void handleClear() {
  String failureReason;
  const bool ok = google_credentials::clear(failureReason);
  noStore();
  if (ok) {
    g_server->send(200, "application/json", "{\"ok\":true}");
  } else {
    String body = "{\"ok\":false,\"error\":\"";
    body += jsonEscape(failureReason);
    body += "\"}";
    g_server->send(500, "application/json", body);
  }
}

}  // namespace

void attachRoutes(WebServer& server,
                  const config_portal::Config& portalConfig) {
  g_server = &server;
  g_pageStart = config_portal::renderPageStartHtml(
      portalConfig, "Google credentials", "google");
  server.on("/google-credentials", HTTP_GET, sendPage);
  server.on("/google-credentials", HTTP_POST, handleUploadDone,
            handleUploadStream);
  server.on("/google-credentials.json", HTTP_GET, sendStatus);
  server.on("/google-credentials/clear", HTTP_POST, handleClear);
}

}  // namespace google_credentials_portal
