#ifdef ARDUINO
#include "config_portal.h"

#include "timezone_list.h"

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
    case FieldType::Timezone: return "timezone";
  }
  return "string";
}

void appendNavLinks(String& html, const Config& cfg, bool settings, const char* active) {
  html += F("<a");
  if (String(active) == "wifi") html += F(" class=\"active\"");
  html += F(" href=\"/wifi\">Wi-Fi</a>");
  if (settings) {
    html += F("<a");
    if (String(active) == "settings") html += F(" class=\"active\"");
    html += F(" href=\"/settings\">Settings</a>");
  }
  for (size_t i = 0; i < cfg.extraTabCount; ++i) {
    const NavTab& tab = cfg.extraTabs[i];
    if (!tab.label || !tab.href) continue;
    html += F("<a");
    if (tab.activeKey && active && String(active) == tab.activeKey) {
      html += F(" class=\"active\"");
    }
    html += F(" href=\"");
    html += htmlEscape(tab.href);
    html += F("\">");
    html += htmlEscape(tab.label);
    html += F("</a>");
  }
  html += F("<a");
  if (String(active) == "reset") html += F(" class=\"active\"");
  html += F(" href=\"/reset\">Reset</a>");
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
            ".ssid-list{list-style:none;margin:.5rem 0 0;padding:0;border:1px solid #d7dce5;border-radius:7px;overflow:hidden;max-height:14rem;overflow-y:auto}"
            ".ssid-list li{display:flex;justify-content:space-between;align-items:center;gap:.5rem;padding:.55rem .75rem;border-bottom:1px solid #eef1f5;cursor:pointer}"
            ".ssid-list li:last-child{border-bottom:0}.ssid-list li:hover,.ssid-list li:focus{background:#eef2ff;outline:none}"
            ".ssid-list .ssid-name{font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
            ".ssid-list .ssid-meta{font-size:.85rem;color:#64748b;flex-shrink:0}"
            "@media(prefers-color-scheme:dark){body{background:#0f172a;color:#e5e7eb}header{background:#020617}.card,fieldset{background:#111827;border-color:#334155}input[type=text],input[type=password],input[type=number],select{background:#0f172a;color:#e5e7eb;border-color:#475569}.help{color:#94a3b8}.ssid-list{border-color:#334155}.ssid-list li{border-bottom-color:#1f2937}.ssid-list li:hover,.ssid-list li:focus{background:#1e293b}.ssid-list .ssid-meta{color:#94a3b8}}"
            "</style></head><body><header><nav>");
  appendNavLinks(html, cfg, settings, active);
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
  } else if (f.type == FieldType::Timezone) {
    // Two controls, one hidden at a time: a <select> of curated
    // POSIX-TZ presets and a text input for the "Custom (POSIX)"
    // fallback. Only the select carries `name`; the shared JS reads
    // the input via the wrapping .field container and submits either
    // the selected preset or the text depending on the select value.
    html += F("<select id=\"");
    html += htmlEscape(f.key);
    html += F("\" name=\"");
    html += htmlEscape(f.key);
    html += F("\" onchange=\"tzToggle(this)\">");
    for (size_t i = 0; i < kTimezoneOptionCount; ++i) {
      html += F("<option value=\"");
      html += htmlEscape(kTimezoneOptions[i].posix);
      html += F("\">");
      html += htmlEscape(kTimezoneOptions[i].label);
      html += F("</option>");
    }
    html += F("<option value=\"");
    html += kTimezoneCustomSentinel;
    html += F("\">Custom (POSIX)</option></select>"
              "<input type=\"text\" data-tz-custom placeholder=\"POSIX TZ string\" "
              "style=\"margin-top:.35rem;display:none\">");
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
function tzCustomInput(el){const c=el.closest?el.closest('.field'):null;return c?c.querySelector('input[data-tz-custom]'):null;}
function tzToggle(sel){const t=tzCustomInput(sel);if(!t)return;t.style.display=(sel.value==='__custom__')?'':'none';}
function valOf(el,type){if(type==='bool')return el.checked?'true':'false';if(type==='timezone'){if(el.value==='__custom__'){const t=tzCustomInput(el);return t?t.value:'';}return el.value;}return el.value;}
function setVal(el,type,v){if(type==='bool'){el.checked=(v==='true'||v==='1'||v==='on'||v==='yes');return;}if(type==='timezone'){let matched=false;for(const opt of el.options){if(opt.value===v){el.value=v;matched=true;break;}}if(!matched){el.value='__custom__';}const t=tzCustomInput(el);if(t){t.value=matched?'':(v||'');t.style.display=(el.value==='__custom__')?'':'none';}return;}if((type==='secret'||type==='password')&&v==='__saved__')el.placeholder='●●●● saved';el.value=v||'';}
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
        html += F("<div class=\"field\" data-type=\"string\"><label for=\"ssid\">SSID</label><div style=\"display:flex;gap:.5rem\"><input type=\"text\" id=\"ssid\" name=\"ssid\"><button type=\"button\" class=\"secondary\" id=\"scanBtn\">Scan</button></div><ul id=\"ssidList\" class=\"ssid-list\" hidden></ul></div>");
      } else {
        appendFieldInput(html, f);
      }
    }
  }
  html += F("<button type=\"submit\">Save Wi-Fi</button><button class=\"secondary\" type=\"button\" id=\"rebootBtn\">Reboot to viewer</button><span id=\"msg\" class=\"msg\"></span></form></section><script>");
  html += FPSTR(kSharedScript);
  html += F(
      "loadValues('/wifi.json');"
      "function rssiBars(r){if(r>=-55)return '\u2588\u2588\u2588\u2588';if(r>=-65)return '\u2588\u2588\u2588_';if(r>=-75)return '\u2588\u2588__';if(r>=-85)return '\u2588___';return '____';}"
      "function renderScan(nets){"
      "let ul=document.getElementById('ssidList');ul.innerHTML='';"
      "let seen=new Set();"
      "nets.slice().sort((a,b)=>b.rssi-a.rssi).forEach(n=>{"
      "if(!n.ssid||seen.has(n.ssid))return;seen.add(n.ssid);"
      "let li=document.createElement('li');li.tabIndex=0;li.setAttribute('role','button');"
      "let name=document.createElement('span');name.className='ssid-name';name.textContent=(n.secure?'\u2022 ':'  ')+n.ssid;"
      "let meta=document.createElement('span');meta.className='ssid-meta';meta.textContent=rssiBars(n.rssi)+' '+n.rssi+' dBm';"
      "li.append(name,meta);"
      "let pick=()=>{let s=document.getElementById('ssid');s.value=n.ssid;s.focus();let p=document.querySelector('[name=\"password\"]');if(p)p.focus();};"
      "li.onclick=pick;li.onkeydown=e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();pick();}};"
      "ul.appendChild(li);});"
      "ul.hidden=ul.children.length===0;"
      "}"
      "async function doScan(){let m=document.getElementById('msg');m.className='';m.textContent='Scanning\u2026';try{let j=await (await fetch('/scan.json',{cache:'no-store'})).json();renderScan(j);m.textContent=j.length?(j.length+' networks found'):'No networks found';}catch(e){m.textContent=e.message;m.className='err'}}"
      "document.getElementById('scanBtn').onclick=doScan;"
      "doScan();"
      "document.getElementById('wifiForm').onsubmit=async e=>{e.preventDefault();let m=document.getElementById('msg');try{await postForm('/wifi.json');m.className='ok';m.textContent='Saved.';}catch(x){m.className='err';m.textContent=x.message}};"
      "document.getElementById('rebootBtn').onclick=async()=>{let m=document.getElementById('msg');m.className='';m.textContent='Rebooting\u2026';try{await fetch('/reboot',{method:'POST'});}catch(e){}};"
      "</script></main></body></html>");
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

String renderResetPage(const Config& cfg, bool hasSettings) {
  String html;
  html.reserve(5000);
  appendChrome(html, cfg, "Restore defaults", hasSettings, "reset");
  html += F("<section class=\"card\"><h2>Restore defaults</h2>"
            "<p>Wipe all saved Wi-Fi credentials and app settings, then reboot. "
            "The device will come back with its compile-time defaults from "
            "<code>secrets.h</code> and <code>config.h</code>.</p>"
            "<ul class=\"help\">"
            "<li>Clears the <b>wifi</b> NVS namespace (SSID + password).</li>");
  if (hasSettings) {
    html += F("<li>Clears the app settings NVS namespace (sleep interval, "
              "quiet hours, NTP, display, debug flags).</li>");
  }
  html += F("<li>Leaves the SoftAP password and cached comic index alone.</li>"
            "<li>This cannot be undone from the portal.</li>"
            "</ul>"
            "<p><label style=\"font-weight:650\">"
            "<input type=\"checkbox\" id=\"confirmChk\" style=\"width:auto;margin-right:.5rem\">"
            "I understand this will erase my saved configuration."
            "</label></p>"
            "<button class=\"secondary\" type=\"button\" id=\"resetBtn\" disabled "
            "style=\"background:#b91c1c\">Restore defaults and reboot</button>"
            "<button class=\"secondary\" type=\"button\" id=\"rebootBtn\">Reboot to viewer</button>"
            "<span id=\"msg\" class=\"msg\"></span></section>");
  if (cfg.sdFormat) {
    html += F("<section class=\"card\"><h2>Erase SD card</h2>"
              "<p>Reformat the microSD card as a fresh FAT32 volume with a new "
              "MBR partition table. Use this to recover a card the device can't "
              "mount, or to wipe it before handing the device to someone else.</p>"
              "<ul class=\"help\">"
              "<li><b>All files on the card will be lost</b>");
    if (cfg.sdFormatWarning && cfg.sdFormatWarning[0]) {
      html += F(" (");
      html += htmlEscape(cfg.sdFormatWarning);
      html += F(")");
    }
    html += F(".</li>"
              "<li>The card is remounted in place; you don't need to remove it.</li>"
              "<li>This cannot be undone.</li>"
              "</ul>"
              "<p><label style=\"font-weight:650\">"
              "Type <code>FORMAT</code> to confirm: "
              "<input type=\"text\" id=\"fmtConfirm\" autocomplete=\"off\" "
              "style=\"width:8rem;margin-left:.5rem\" placeholder=\"FORMAT\">"
              "</label></p>"
              "<button class=\"secondary\" type=\"button\" id=\"fmtBtn\" disabled "
              "style=\"background:#b91c1c\">Erase SD card</button>"
              "<span id=\"fmtMsg\" class=\"msg\"></span></section>");
  }
  html += F("<script>"
            "let btn=document.getElementById('resetBtn');"
            "document.getElementById('confirmChk').onchange=e=>{btn.disabled=!e.target.checked;};"
            "btn.onclick=async()=>{let m=document.getElementById('msg');m.className='';"
            "m.textContent='Wiping saved config\u2026';btn.disabled=true;"
            "try{let r=await fetch('/reset.json',{method:'POST'});let j=await r.json();"
            "if(!r.ok||!j.ok)throw new Error(j.error||'reset failed');"
            "m.className='ok';m.textContent='Done. Rebooting\u2026';"
            "}catch(x){m.className='err';m.textContent=x.message;btn.disabled=false;}};"
            "document.getElementById('rebootBtn').onclick=async()=>{let m=document.getElementById('msg');m.className='';m.textContent='Rebooting\u2026';try{await fetch('/reboot',{method:'POST'});}catch(e){}};");
  if (cfg.sdFormat) {
    html += F("let fb=document.getElementById('fmtBtn');"
              "let fi=document.getElementById('fmtConfirm');"
              "fi.oninput=()=>{fb.disabled=fi.value.trim()!=='FORMAT';};"
              "fb.onclick=async()=>{let m=document.getElementById('fmtMsg');m.className='';"
              "m.textContent='Formatting SD card\u2026 this may take up to a minute.';fb.disabled=true;"
              "try{let r=await fetch('/format-sd.json',{method:'POST'});let j=await r.json();"
              "if(!r.ok||!j.ok)throw new Error(j.error||'format failed');"
              "m.className='ok';m.textContent='SD card reformatted.';fi.value='';"
              "}catch(x){m.className='err';m.textContent=x.message;fb.disabled=fi.value.trim()!=='FORMAT';}};");
  }
  html += F("</script></main></body></html>");
  return html;
}

String renderNavStripHtml(const Config& cfg, const char* activeKey) {
  String html;
  html.reserve(256);
  html += F("<nav>");
  appendNavLinks(html, cfg, cfg.appSchema != nullptr, activeKey ? activeKey : "");
  html += F("</nav>");
  return html;
}

}  // namespace config_portal
#endif
