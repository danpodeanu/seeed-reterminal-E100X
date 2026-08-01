#pragma once

#include <stddef.h>

// Curated list of POSIX timezone strings for the portal's timezone
// dropdown. The label is what the user sees ("London (GMT/BST)"), the
// posix string is what gets stored in NVS and fed to setenv("TZ", ...).
//
// Coverage goal: every major DST regime (EU/UK, US, AU, NZ) plus the
// largest no-DST populations, in ~25 entries. The portal always adds a
// "Custom (POSIX)" option after this list so users in a country we did
// not curate can still enter a raw POSIX string.
namespace config_portal {

struct TimezoneOption {
  const char* label;
  const char* posix;
};

extern const TimezoneOption kTimezoneOptions[];
extern const size_t kTimezoneOptionCount;

// Sentinel value used by the <select>'s "Custom (POSIX)" option. When
// the browser posts this, the accompanying text input carries the real
// POSIX string.
constexpr const char* kTimezoneCustomSentinel = "__custom__";

// Returns the label for a given posix string, or nullptr if the string
// is not one of the curated presets.
const char* timezoneLabelFor(const char* posix);

// True if `posix` matches one of the curated preset values exactly.
bool timezoneIsPreset(const char* posix);

}  // namespace config_portal
