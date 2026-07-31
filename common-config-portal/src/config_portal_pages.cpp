#ifdef ARDUINO
#include "config_portal.h"

namespace config_portal {
namespace {

String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    switch (in[i]) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += in[i]; break;
    }
  }
  return out;
}

const char* typeName(FieldType t) {
  switch (t) {
    case FieldType::Bool: return "bool";
    case FieldType::Int: return "int";
    case FieldType::Float: return "float";
    case FieldType::String: return "string";
    case FieldType::Enum: return "enum";
    case FieldType::Secret: return "secret";
    case FieldType::Password: return "password";
  }
  return "string";
}

void appendChrome(String& html, const Config& cfg, const char* title,
                  bool settings, const char* active) {
  html += F("<!doctype html><html><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>");
  html += htmlEscape(title);
  html += F("</title><style>"
            "*{box-sizing:border-box}body{font-family:system-ui,-apple-system,sans-serif;margin:0;background:#f6f7f9;color:#172033;line-height:1.45}"
            "header{padding:1rem;background:#14213d;color:#fff}main{max-width:760px;margin:auto;padding:1rem}"
            "nav a{color:#bfdbfe;margin-right:1rem;text-decoration:none}nav a.active{color:#fff;font-weight:700}"
            ".card,fieldset{background:#fff;border:1px solid #d7dce5;border-radius:10px;padding:1rem;margin:0 0 1rem}"
            "label{display:block;font-weight:650;margin:.8rem 0 .25rem}input,select,button{font:inherit;font-size:1rem}"
            "input[type=text],input[type=password],input[type=number],select{width:100%;padding:.65rem;border:1px solid #b8c0cc;border-radius:7px;background:#fff;color:#111827}"
            "button{background:#1d4ed8;color:#fff;border:0;border-radius:7px;padding:.7rem 1rem;min-height:44px;cursor:pointer;margin:.4rem .4rem .4rem 0}"
            "button.secondary{background:#475569}.help{font-size:.9rem;color:#64748b}.msg{min-height:1.5rem}.err{color:#b91c1c}.ok{color:#166534}"
            "@media(prefers-color-scheme:dark){body{background:#0f172a;color:#e5e7eb}header{background:#020617}.card,fieldset{background:#111827;border-color:#334155}input[type=text],input[type=password],input[type=number],select{background:#0f172a;color:#e5e7eb;border-color:#475569}.help{color:#94a3b8}}"
            "</style></head><body><header><nav><a");
  if (String(active) == "wifi") html += F(" class=\"active\"");
  html += F(" href=\"/wifi\">Wi-Fi</a>");
  if (settings) {
    html += F("<a");
    if (String(active) == "settings") html += F(" class=\"active\"");
    html += F(" href=\"/settings\">Settings</a>");
  }
  html += F("</nav><h1>");
  html += htmlEscape(cfg.appName ? cfg.appName : "reTerminal");
  html += F("</h1><p>");
  html += htmlEscape(currentSsid());
  html += F(" · ");
  html += cfg.apIp.toString();
  if (cfg.firmwareVersion && cfg.firmwareVersion[0]) {
    html += F(" · firmware ");
    html += htmlEscape(cfg.firmwareVersion);
  }
  html += F("</p></header><main>");
}

void appendFieldInput(String& html, const Field& f) {
  html += F("<div class=\"field\" data-type=\"");
  html += typeName(f.type);
  html += F("\"><label for=\"");
  html += htmlEscape(f.key);
  html += F("\">");
  html += htmlEscape(f.label ? f.label : f.key);
  html += F("</label>");
  if (f.type == FieldType::Bool) {
    html += F("<input type=\"checkbox\" id=\"");
    html += htmlEscape(f.key);
    html += F("\" name=\"");
    html += htmlEscape(f.key);
    html += F("\">");
  } else if (f.type == FieldType::Enum) {
    html += F("<select id=\"");
    html += htmlEscape(f.key);
    html += F("\" name=\"");
    html += htmlEscape(f.key);
    html += F("\">");
    if (f.enumValues) {
      for (const char* const* p = f.enumValues; *p; ++p) {
        html += F("<option value=\"");
        html += htmlEscape(*p);
        html += F("\">");
        html += htmlEscape(*p);
        html += F("</option>");
      }
    }
    html += F("</select>");
  } else {
    html += F("<input id=\"");
    html += htmlEscape(f.key);
    html += F("\" name=\"");
    html += htmlEscape(f.key);
    html += F("\" type=\"");
    if (f.type == FieldType::Int || f.type == FieldType::Float) html += F("number");
    else if (f.type == FieldType::Secret || f.type == FieldType::Password) html += F("password");
    else html += F("text");
    html += F("\"");
    if (f.type == FieldType::Float) html += F(" step=\"any\"");
    if (f.type == FieldType::Int && (f.minVal || f.maxVal)) {
      html += F(" min=\""); html += f.minVal; html += F("\" max=\""); html += f.maxVal; html += F("\"");
    }
    html += F(">");
  }
  if (f.helpText && f.helpText[0]) {
    html += F("<div class=\"help\">");
    html += htmlEscape(f.helpText);
    html += F("</div>");
  }
  html += F("</div>");
}

const char kSharedScript[] PROGMEM = R"JS(
function valOf(el,type){if(type==='bool')return el.checked?'true':'false';return el.value;}
function setVal(el,type,v){if(type==='bool')el.checked=(v==='true'||v==='1'||v==='on'||v==='yes');else{if((type==='secret'||type==='password')&&v==='__saved__')el.placeholder='●●●● saved';el.value=v||'';}}
async function loadValues(url){const r=await fetch(url,{cache:'no-store'});const j=await r.json();for(const [k,v] of Object.entries(j.values||{})){const el=document.querySelector('[name="'+CSS.escape(k)+'"]');if(el)setVal(el,el.closest('.field')?.dataset.type||'string',v);}}
async function postForm(url){const data={};document.querySelectorAll('[name]').forEach(el=>{data[el.name]=valOf(el,el.closest('.field')?.dataset.type||'string')});const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});const j=await r.json();if(!r.ok||!j.ok)throw new Error(j.error||'Save failed');return j;}
)JS";

}  // namespace

String renderWifiPage(const Config& cfg, const Schema& wifi, const Schema* appSchema) {
  String html;
  html.reserve(7000);
  appendChrome(html, cfg, "Wi-Fi configuration", appSchema != nullptr, "wifi");
  html += F("<section class=\"card\"><h2>Wi-Fi</h2><form id=\"wifiForm\">");
  for (size_t si = 0; si < wifi.sectionCount; ++si) {
    for (size_t fi = 0; fi < wifi.sections[si].fieldCount; ++fi) {
      const Field& f = wifi.sections[si].fields[fi];
      if (String(f.key) == "ssid") {
        html += F("<div class=\"field\" data-type=\"string\"><label for=\"ssid\">SSID</label><div style=\"display:flex;gap:.5rem\"><input type=\"text\" list=\"ssidList\" id=\"ssid\" name=\"ssid\"><datalist id=\"ssidList\"></datalist><button type=\"button\" class=\"secondary\" id=\"scanBtn\">Scan</button></div></div>");
      } else {
        appendFieldInput(html, f);
      }
    }
  }
  html += F("<button type=\"submit\">Save Wi-Fi</button><span id=\"msg\" class=\"msg\"></span></form></section><script>");
  html += FPSTR(kSharedScript);
  html += F("loadValues('/wifi.json');document.getElementById('scanBtn').onclick=async()=>{let m=document.getElementById('msg');m.textContent='Scanning…';try{let j=await (await fetch('/scan.json',{cache:'no-store'})).json();let dl=document.getElementById('ssidList');dl.innerHTML='';j.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;o.label=n.ssid+' ('+n.rssi+' dBm)'+(n.secure?' 🔒':'');dl.appendChild(o)});m.textContent='';}catch(e){m.textContent=e.message;m.className='err'}};document.getElementById('wifiForm').onsubmit=async e=>{e.preventDefault();let m=document.getElementById('msg');try{await postForm('/wifi.json');m.className='ok';m.textContent='Saved. Rebooting…';setTimeout(()=>location.reload(),2000)}catch(x){m.className='err';m.textContent=x.message}};</script></main></body></html>");
  return html;
}

String renderSettingsPage(const Config& cfg, const Schema& appSchema, const Schema& /*wifi*/) {
  String html;
  html.reserve(9000);
  appendChrome(html, cfg, "Settings", true, "settings");
  html += F("<form id=\"settingsForm\">");
  for (size_t si = 0; si < appSchema.sectionCount; ++si) {
    const Section& section = appSchema.sections[si];
    html += F("<fieldset><legend>");
    html += htmlEscape(section.title ? section.title : "Settings");
    html += F("</legend>");
    for (size_t fi = 0; fi < section.fieldCount; ++fi) appendFieldInput(html, section.fields[fi]);
    html += F("</fieldset>");
  }
  html += F("<button type=\"submit\">Save settings</button><button class=\"secondary\" type=\"button\" id=\"rebootBtn\">Reboot to viewer</button><span id=\"msg\" class=\"msg\"></span></form><script>");
  html += FPSTR(kSharedScript);
  html += F("loadValues('/settings.json');document.getElementById('settingsForm').onsubmit=async e=>{e.preventDefault();let m=document.getElementById('msg');try{await postForm('/settings.json');m.className='ok';m.textContent='Saved.'}catch(x){m.className='err';m.textContent=x.message}};document.getElementById('rebootBtn').onclick=async()=>{await fetch('/reboot',{method:'POST'});document.getElementById('msg').textContent='Rebooting…';setTimeout(()=>location.reload(),2000)};</script></main></body></html>");
  return html;
}

}  // namespace config_portal
#endif
