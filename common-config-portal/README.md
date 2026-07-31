# common-config-portal

Reusable Wi-Fi + secrets + settings configuration portal for reTerminal E100X viewer apps. It starts an ESP32 soft-AP, serves a captive web portal, renders forms from `config_portal::Schema`, and persists values to NVS with one namespace per schema.

## Viewer wire-up

```cpp
#include "config_portal.h"
#include "wifi_schema.h"

const char* const kModes[] = {"random", "latest", nullptr};
const config_portal::Field kAppFields[] = {
    {"mode", "Display mode", nullptr, config_portal::FieldType::Enum,
     "random", kModes, 0, 0, nullptr},
};
const config_portal::Section kAppSections[] = {{"XKCD", kAppFields, 1}};
const config_portal::Schema kAppSchema = {"xkcd", kAppSections, 1};

void startPortal() {
  config_portal::Config cfg;
  cfg.wifiSchema = &config_portal::kWifiSchema;
  cfg.appSchema = &kAppSchema;
  cfg.appName = "XKCD Viewer";
  cfg.firmwareVersion = FW_VERSION;
  config_portal::begin(cfg);
}
```

Call `config_portal::loop()` from `loop()` while the portal is active, then reboot or exit when `config_portal::rebootRequested()` becomes true.

## HTTP routes

- `GET /` redirects to `/wifi`.
- `GET /wifi`, `GET /settings` render standalone HTML pages.
- `GET /wifi.json`, `GET /settings.json` return current values as `{"ok":true,"values":{...}}`.
- `POST /wifi.json`, `POST /settings.json` accept JSON objects of field key/value pairs.
- `GET /scan.json` returns scanned networks: `[{"ssid":"...","rssi":-55,"secure":true}]`.
- `POST /reboot` sets the reboot flag.
- `GET /panel.json` returns app/portal capability metadata.
- Captive probe paths redirect to `/wifi`; other unknown paths return a small 404 page.

## NVS layout

Each schema uses its own `Schema::nvsNamespace` (15 characters or fewer). Each field key is also its NVS key and must also be 15 characters or fewer. Unset fields read back as `Field::defaultVal`.

## Secrets and redaction

`Secret` and `Password` fields store raw strings in NVS, but `GET *.json` returns `config_portal::kSecretSentinel` (`__saved__`) whenever a stored secret is non-empty. On POST, `__saved__` preserves the existing value, any other non-empty value overwrites it, and an empty value clears the NVS key.

## Adding a field

Add a `config_portal::Field` to a section, keep the key short, choose a `FieldType`, provide a string default, and use `enumValues`, integer bounds, or a literal `pattern` when needed. Pattern matching is intentionally simple: the value must contain the pattern substring.

## Native tests

From `common-config-portal\` run:

```powershell
pio test -c platformio-test.ini -e native_test
```
