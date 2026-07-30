#pragma once

// User-tweakable configuration for the SD-card Wi-Fi portal. Keep this
// header small on purpose: any deployment tweak that is likely to
// differ between users should live here, everything else should be a
// build-time detail in main.cpp.

namespace config {

// URL that the small third QR code links to. Aim for a page that a
// visitor can read on their phone right after connecting to the AP -
// e.g. the tool's README on GitHub. Kept as a compile-time constant so
// the QR data can be baked into flash.
constexpr char HELP_URL[] =
    "https://github.com/danpodeanu/seeed-reterminal-E100X/tree/main/tools/sd-web";

// Caption printed on the panel above the help QR code. Short - two or
// three words is enough since the QR itself is smaller than the other
// two.
constexpr char HELP_CAPTION[] = "Help";

}  // namespace config
