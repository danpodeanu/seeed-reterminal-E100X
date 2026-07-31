#include "wifi_schema.h"

namespace config_portal {
namespace {

const Field kWifiFields[] = {
    {"ssid", "SSID", "Wi-Fi network name. Leave empty when not configured.",
     FieldType::String, "", nullptr, 0, 32, nullptr},
    {"password", "Password", "Wi-Fi password; empty is allowed for open networks.",
     FieldType::Password, "", nullptr, 0, 63, nullptr},
};

const Section kWifiSections[] = {
    {"Wi-Fi", kWifiFields, sizeof(kWifiFields) / sizeof(kWifiFields[0])},
};

}  // namespace

const Schema kWifiSchema = {"wifi", kWifiSections,
                            sizeof(kWifiSections) / sizeof(kWifiSections[0])};

}  // namespace config_portal
