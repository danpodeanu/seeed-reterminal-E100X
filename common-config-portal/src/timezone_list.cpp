#include "timezone_list.h"

#include <string.h>

namespace config_portal {

const TimezoneOption kTimezoneOptions[] = {
    {"UTC / GMT",                             "UTC0"},
    {"London (GMT/BST)",                      "GMT0BST,M3.5.0/1,M10.5.0/2"},
    {"Dublin (GMT/IST)",                      "GMT0IST,M3.5.0/1,M10.5.0/2"},
    {"Lisbon (WET/WEST)",                     "WET0WEST,M3.5.0/1,M10.5.0/2"},
    {"Paris / Berlin / Rome (CET/CEST)",      "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Athens / Helsinki (EET/EEST)",          "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Cairo (EET/EEST)",                      "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Istanbul / Moscow (+03, no DST)",       "<+03>-3"},
    {"Dubai (+04, no DST)",                   "<+04>-4"},
    {"Karachi (PKT)",                         "PKT-5"},
    {"Delhi (IST +5:30)",                     "IST-5:30"},
    {"Bangkok / Jakarta (+07)",               "<+07>-7"},
    {"Beijing / Shanghai / Singapore (+08)",  "CST-8"},
    {"Perth (AWST)",                          "AWST-8"},
    {"Tokyo / Seoul (+09)",                   "JST-9"},
    {"Sydney / Melbourne (AEST/AEDT)",        "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Auckland (NZST/NZDT)",                  "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Honolulu (HST, no DST)",                "HST10"},
    {"Anchorage (AKST/AKDT)",                 "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Los Angeles (PST/PDT)",                 "PST8PDT,M3.2.0,M11.1.0"},
    {"Phoenix (MST, no DST)",                 "MST7"},
    {"Denver (MST/MDT)",                      "MST7MDT,M3.2.0,M11.1.0"},
    {"Chicago (CST/CDT)",                     "CST6CDT,M3.2.0,M11.1.0"},
    {"New York (EST/EDT)",                    "EST5EDT,M3.2.0,M11.1.0"},
    {"S\xC3\xA3o Paulo (-03, no DST)",        "<-03>3"},
    {"Nairobi (EAT)",                         "EAT-3"},
};

const size_t kTimezoneOptionCount =
    sizeof(kTimezoneOptions) / sizeof(kTimezoneOptions[0]);

const char* timezoneLabelFor(const char* posix) {
  if (!posix || !posix[0]) return nullptr;
  for (size_t i = 0; i < kTimezoneOptionCount; ++i) {
    if (strcmp(kTimezoneOptions[i].posix, posix) == 0) {
      return kTimezoneOptions[i].label;
    }
  }
  return nullptr;
}

bool timezoneIsPreset(const char* posix) {
  return timezoneLabelFor(posix) != nullptr;
}

}  // namespace config_portal
