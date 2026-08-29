#include "ical_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include "local_time.h"

namespace ical {
namespace {

struct ParsedProperty {
  std::string name;
  std::string parameters;
  std::string value;
};

struct RawEvent {
  std::string uid;
  std::string title;
  std::string location;
  std::string rule;
  std::vector<time_t> excluded;
  time_t start = 0;
  time_t end = 0;
  time_t recurrenceId = 0;
  time_t durationSeconds = 0;
  int durationDays = 0;
  uint32_t color = 0;
  std::string timezone;
  bool haveColor = false;
  bool haveStart = false;
  bool haveEnd = false;
  bool haveDuration = false;
  bool allDay = false;
  bool utc = false;
  bool cancelled = false;
};

enum class Frequency {
  None,
  Daily,
  Weekly,
  Monthly,
  Yearly,
};

struct Rule {
  Frequency frequency = Frequency::None;
  int interval = 1;
  int count = 0;
  time_t until = 0;
  uint8_t weekdays = 0;
  std::vector<std::pair<int, int>> ordinalWeekdays;
  std::vector<int> monthDays;
  int weekStart = 1;
};

std::string upper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

std::string trim(const std::string& value) {
  size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }
  size_t last = value.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(value[last - 1]))) {
    --last;
  }
  return value.substr(first, last - first);
}

std::vector<std::string> split(const std::string& value, char delimiter) {
  std::vector<std::string> result;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t end = value.find(delimiter, start);
    result.push_back(value.substr(
        start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return result;
}

std::vector<std::string> unfold(const std::string& payload) {
  std::vector<std::string> result;
  std::istringstream input(payload);
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && (line.front() == ' ' || line.front() == '\t') &&
        !result.empty()) {
      result.back().append(line.substr(1));
    } else {
      result.push_back(line);
    }
  }
  return result;
}

bool parseProperty(const std::string& line, ParsedProperty& out) {
  const size_t colon = line.find(':');
  if (colon == std::string::npos) return false;
  const std::string key = line.substr(0, colon);
  const size_t semicolon = key.find(';');
  out.name = upper(key.substr(0, semicolon));
  out.parameters = semicolon == std::string::npos
                       ? std::string()
                       : upper(key.substr(semicolon + 1));
  out.value = line.substr(colon + 1);
  return true;
}

bool parameterValue(const ParsedProperty& property, const char* name,
                    std::string& value) {
  const std::string wanted = upper(name == nullptr ? "" : name);
  for (const std::string& parameter : split(property.parameters, ';')) {
    const size_t equals = parameter.find('=');
    if (equals == std::string::npos ||
        trim(parameter.substr(0, equals)) != wanted) {
      continue;
    }
    value = trim(parameter.substr(equals + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    return true;
  }
  value.clear();
  return false;
}

std::string unescapeText(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] != '\\' || i + 1 >= value.size()) {
      result.push_back(value[i]);
      continue;
    }
    const char escaped = value[++i];
    if (escaped == 'n' || escaped == 'N') {
      result.push_back(' ');
    } else {
      result.push_back(escaped);
    }
  }
  return trim(result);
}

bool parseDigits(const std::string& value, size_t offset, size_t length,
                 int& out) {
  if (offset + length > value.size()) return false;
  int parsed = 0;
  for (size_t i = 0; i < length; ++i) {
    const char c = value[offset + i];
    if (c < '0' || c > '9') return false;
    parsed = parsed * 10 + (c - '0');
  }
  out = parsed;
  return true;
}

bool leapYear(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int daysInMonth(int year, int month) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30,
                              31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && leapYear(year) ? 29 : kDays[month - 1];
}

int64_t daysFromCivil(int year, int month, int day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy =
      (153U * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2U) /
          5U +
      static_cast<unsigned>(day) - 1U;
  const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) -
         719468;
}

class ScopedTimezone {
 public:
  explicit ScopedTimezone(const std::string& timezone) {
    if (timezone.empty()) return;
    const char* current = getenv("TZ");
    hadPrevious_ = current != nullptr;
    if (hadPrevious_) previous_ = current;
    if (previous_ == timezone) return;
#ifdef _WIN32
    _putenv_s("TZ", timezone.c_str());
#else
    setenv("TZ", timezone.c_str(), 1);
#endif
    tzset();
    changed_ = true;
  }

  ~ScopedTimezone() {
    if (!changed_) return;
#ifdef _WIN32
    _putenv_s("TZ", hadPrevious_ ? previous_.c_str() : "");
#else
    if (hadPrevious_) {
      setenv("TZ", previous_.c_str(), 1);
    } else {
      unsetenv("TZ");
    }
#endif
    tzset();
  }

 private:
  std::string previous_;
  bool hadPrevious_ = false;
  bool changed_ = false;
};

bool endsWithTimezone(const std::string& value, const char* suffix) {
  const size_t suffixLength = std::strlen(suffix);
  return value == suffix ||
         (value.size() > suffixLength &&
         value.compare(value.size() - suffixLength, suffixLength, suffix) ==
             0 &&
         value[value.size() - suffixLength - 1] == '/');
}

bool resolveTimezone(const std::string& raw, std::string& posix) {
  const std::string timezone = upper(trim(raw));
  struct Mapping {
    const char* id;
    const char* posix;
  };
  static const Mapping kMappings[] = {
      {"UTC", "UTC0"},
      {"ETC/UTC", "UTC0"},
      {"ETC/GMT", "UTC0"},
      {"EUROPE/LONDON", "GMT0BST,M3.5.0/1,M10.5.0/2"},
      {"EUROPE/DUBLIN", "GMT0IST,M3.5.0/1,M10.5.0/2"},
      {"EUROPE/LISBON", "WET0WEST,M3.5.0/1,M10.5.0/2"},
      {"EUROPE/PARIS", "CET-1CEST,M3.5.0,M10.5.0/3"},
      {"EUROPE/BERLIN", "CET-1CEST,M3.5.0,M10.5.0/3"},
      {"EUROPE/ROME", "CET-1CEST,M3.5.0,M10.5.0/3"},
      {"EUROPE/MADRID", "CET-1CEST,M3.5.0,M10.5.0/3"},
      {"EUROPE/ATHENS", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
      {"EUROPE/HELSINKI", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
      {"AFRICA/CAIRO", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
      {"EUROPE/ISTANBUL", "<+03>-3"},
      {"EUROPE/MOSCOW", "<+03>-3"},
      {"ASIA/DUBAI", "<+04>-4"},
      {"ASIA/KARACHI", "PKT-5"},
      {"ASIA/DELHI", "IST-5:30"},
      {"ASIA/KOLKATA", "IST-5:30"},
      {"ASIA/CALCUTTA", "IST-5:30"},
      {"ASIA/BANGKOK", "<+07>-7"},
      {"ASIA/JAKARTA", "<+07>-7"},
      {"ASIA/BEIJING", "CST-8"},
      {"ASIA/SHANGHAI", "CST-8"},
      {"ASIA/HONG_KONG", "CST-8"},
      {"ASIA/SINGAPORE", "CST-8"},
      {"AUSTRALIA/PERTH", "AWST-8"},
      {"ASIA/TOKYO", "JST-9"},
      {"ASIA/SEOUL", "JST-9"},
      {"AUSTRALIA/SYDNEY", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
      {"AUSTRALIA/MELBOURNE", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
      {"PACIFIC/AUCKLAND", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
      {"PACIFIC/HONOLULU", "HST10"},
      {"AMERICA/ANCHORAGE", "AKST9AKDT,M3.2.0,M11.1.0"},
      {"AMERICA/LOS_ANGELES", "PST8PDT,M3.2.0,M11.1.0"},
      {"AMERICA/VANCOUVER", "PST8PDT,M3.2.0,M11.1.0"},
      {"AMERICA/PHOENIX", "MST7"},
      {"AMERICA/DENVER", "MST7MDT,M3.2.0,M11.1.0"},
      {"AMERICA/CHICAGO", "CST6CDT,M3.2.0,M11.1.0"},
      {"AMERICA/NEW_YORK", "EST5EDT,M3.2.0,M11.1.0"},
      {"AMERICA/TORONTO", "EST5EDT,M3.2.0,M11.1.0"},
      {"AMERICA/SAO_PAULO", "<-03>3"},
      {"AFRICA/NAIROBI", "EAT-3"},
  };
  for (const Mapping& mapping : kMappings) {
    if (endsWithTimezone(timezone, mapping.id)) {
      posix = mapping.posix;
      return true;
    }
  }
  const char* current = getenv("TZ");
  if (current != nullptr && timezone == upper(current)) {
    posix = current;
    return true;
  }
  posix.clear();
  return false;
}

bool parseDateTime(const std::string& raw, bool forceDate, time_t& out,
                  bool& allDay, bool* utcOut = nullptr,
                  const std::string& timezone = std::string()) {
  const std::string value = trim(raw);
  allDay = forceDate || (value.size() == 8 && value.find('T') == std::string::npos);
  const bool utc = !value.empty() && value.back() == 'Z';
  const bool timezoneIsUtc = timezone == "UTC0";
  if (utcOut != nullptr) *utcOut = utc || timezoneIsUtc;
  const size_t coreLength = value.size() - (utc ? 1U : 0U);
  if ((allDay && (utc || coreLength != 8)) ||
      (!allDay && coreLength != 13 && coreLength != 15)) {
    return false;
  }
  int year = 0;
  int month = 0;
  int day = 0;
  if (!parseDigits(value, 0, 4, year) ||
      !parseDigits(value, 4, 2, month) ||
      !parseDigits(value, 6, 2, day) ||
      year < 1970 || month < 1 || month > 12 ||
      day < 1 || day > daysInMonth(year, month)) {
    return false;
  }

  int hour = 0;
  int minute = 0;
  int second = 0;
  if (!allDay) {
    if (value.size() < 13 || value[8] != 'T' ||
        !parseDigits(value, 9, 2, hour) ||
        !parseDigits(value, 11, 2, minute)) {
      return false;
    }
    if (value.size() >= 15 && !parseDigits(value, 13, 2, second)) return false;
    if (hour > 23 || minute > 59 || second > 59) return false;
  }

  if (utc) {
    out = static_cast<time_t>(daysFromCivil(year, month, day) * 86400LL +
                              hour * 3600LL + minute * 60LL + second);
    return out > 0;
  }

  ScopedTimezone selectedTimezone(timezone);
  struct tm local = {};
  local.tm_year = year - 1900;
  local.tm_mon = month - 1;
  local.tm_mday = day;
  local.tm_hour = hour;
  local.tm_min = minute;
  local.tm_sec = second;
  local.tm_isdst = -1;
  out = mktime(&local);
  if (out <= 0) return false;
  struct tm roundTrip = {};
  return localtime_r(&out, &roundTrip) != nullptr &&
         roundTrip.tm_year == year - 1900 &&
         roundTrip.tm_mon == month - 1 &&
         roundTrip.tm_mday == day &&
         roundTrip.tm_hour == hour &&
         roundTrip.tm_min == minute &&
         roundTrip.tm_sec == second;
}

bool parseTemporalProperty(const ParsedProperty& property,
                          const std::string& fallbackTimezone,
                          time_t& timestamp, bool& allDay, bool* utcOut,
                          std::string* timezoneOut,
                          std::string& failureReason) {
  std::string valueType;
  const bool hasValueType = parameterValue(property, "VALUE", valueType);
  if (hasValueType && valueType != "DATE" && valueType != "DATE-TIME") {
    failureReason = "Unsupported iCalendar " + property.name + " value type";
    return false;
  }
  const bool forceDate = hasValueType && valueType == "DATE";
  if (hasValueType && valueType == "DATE-TIME" &&
      property.value.find('T') == std::string::npos) {
    failureReason = "Invalid iCalendar " + property.name + " DATE-TIME value";
    return false;
  }
  std::string timezone;
  const bool hasTimezone = parameterValue(property, "TZID", timezone);
  if (forceDate && hasTimezone) {
    failureReason = property.name + " cannot combine VALUE=DATE with TZID";
    return false;
  }
  if (hasTimezone && !property.value.empty() &&
      property.value.back() == 'Z') {
    failureReason = property.name + " cannot combine UTC with TZID";
    return false;
  }
  std::string resolvedTimezone = fallbackTimezone;
  if (hasTimezone && !resolveTimezone(timezone, resolvedTimezone)) {
    failureReason = "Unsupported iCalendar TZID: " + timezone;
    return false;
  }
  if (!parseDateTime(property.value, forceDate, timestamp, allDay, utcOut,
                    resolvedTimezone)) {
    failureReason = "Invalid iCalendar " + property.name + " value";
    return false;
  }
  if (timezoneOut != nullptr) {
    *timezoneOut = forceDate ? std::string() : resolvedTimezone;
  }
  return true;
}

bool parseDuration(const std::string& raw, int& durationDays,
                   time_t& durationSeconds) {
  const std::string value = upper(trim(raw));
  size_t cursor = 0;
  if (!value.empty() && value[cursor] == '+') ++cursor;
  if (cursor >= value.size() || value[cursor] != 'P') return false;
  ++cursor;
  bool inTime = false;
  bool haveComponent = false;
  bool haveWeek = false;
  bool haveDay = false;
  int lastTimeRank = 0;
  int64_t days = 0;
  int64_t seconds = 0;
  while (cursor < value.size()) {
    if (value[cursor] == 'T') {
      if (inTime || haveWeek || cursor + 1 >= value.size()) return false;
      inTime = true;
      ++cursor;
      continue;
    }
    const size_t numberStart = cursor;
    while (cursor < value.size() &&
          std::isdigit(static_cast<unsigned char>(value[cursor]))) {
      ++cursor;
    }
    if (numberStart == cursor || cursor >= value.size()) return false;
    const std::string number =
        value.substr(numberStart, cursor - numberStart);
    char* end = nullptr;
    const unsigned long amount = std::strtoul(number.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') return false;
    const char designator = value[cursor++];
    if (!inTime && designator == 'W' && !haveComponent &&
        cursor == value.size()) {
      if (amount >
          static_cast<unsigned long>(
             (std::numeric_limits<int>::max() - days) / 7)) {
        return false;
      }
      days += static_cast<int64_t>(amount) * 7;
      haveWeek = true;
    } else if (!inTime && designator == 'D' && !haveWeek && !haveDay) {
      if (amount >
          static_cast<unsigned long>(
             std::numeric_limits<int>::max() - days)) {
        return false;
      }
      days += amount;
      haveDay = true;
    } else if (inTime && designator == 'H' && lastTimeRank < 1) {
      if (amount >
          static_cast<unsigned long>(
             (std::numeric_limits<int64_t>::max() - seconds) / 3600LL)) {
        return false;
      }
      seconds += static_cast<int64_t>(amount) * 3600LL;
      lastTimeRank = 1;
    } else if (inTime && designator == 'M' && lastTimeRank < 2) {
      if (amount >
          static_cast<unsigned long>(
             (std::numeric_limits<int64_t>::max() - seconds) / 60LL)) {
        return false;
      }
      seconds += static_cast<int64_t>(amount) * 60LL;
      lastTimeRank = 2;
    } else if (inTime && designator == 'S' && lastTimeRank < 3) {
      if (amount >
          static_cast<unsigned long>(
             std::numeric_limits<int64_t>::max() - seconds)) {
        return false;
      }
      seconds += amount;
      lastTimeRank = 3;
    } else {
      return false;
    }
    haveComponent = true;
  }
  if (!haveComponent || (days == 0 && seconds == 0) ||
      seconds > std::numeric_limits<time_t>::max()) {
    return false;
  }
  durationDays = static_cast<int>(days);
  durationSeconds = static_cast<time_t>(seconds);
  return true;
}

uint32_t parseColor(const std::string& value, uint32_t fallback) {
  std::string hex = trim(value);
  const std::string named = upper(hex);
  static const std::pair<const char*, uint32_t> kNamedColors[] = {
      {"BLACK", 0x000000},   {"SILVER", 0xC0C0C0},
      {"GRAY", 0x808080},    {"WHITE", 0xFFFFFF},
      {"MAROON", 0x800000},  {"RED", 0xFF0000},
      {"PURPLE", 0x800080},  {"FUCHSIA", 0xFF00FF},
      {"GREEN", 0x008000},   {"LIME", 0x00FF00},
      {"OLIVE", 0x808000},   {"YELLOW", 0xFFFF00},
      {"NAVY", 0x000080},    {"BLUE", 0x0000FF},
      {"TEAL", 0x008080},    {"AQUA", 0x00FFFF},
      {"ORANGE", 0xFFA500},
  };
  for (const auto& candidate : kNamedColors) {
    if (named == candidate.first) return candidate.second;
  }
  if (!hex.empty() && hex.front() == '#') hex.erase(hex.begin());
  if (hex.size() == 3) {
    std::string expanded;
    expanded.reserve(6);
    for (const char digit : hex) {
      expanded.push_back(digit);
      expanded.push_back(digit);
    }
    hex = expanded;
  }
  if (hex.size() == 8) hex.resize(6);
  if (hex.size() != 6) return fallback;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(hex.c_str(), &end, 16);
  return end != nullptr && *end == '\0'
             ? static_cast<uint32_t>(parsed)
             : fallback;
}

int weekdayFromToken(const std::string& token) {
  if (token.size() < 2) return -1;
  const std::string day = upper(token.substr(token.size() - 2));
  if (day == "SU") return 0;
  if (day == "MO") return 1;
  if (day == "TU") return 2;
  if (day == "WE") return 3;
  if (day == "TH") return 4;
  if (day == "FR") return 5;
  if (day == "SA") return 6;
  return -1;
}

bool breakDown(time_t value, bool utc, const std::string& timezone,
               struct tm& out) {
  if (utc) {
    const struct tm* valueUtc = gmtime(&value);
    if (valueUtc == nullptr) return false;
    out = *valueUtc;
    return true;
  }
  ScopedTimezone selectedTimezone(timezone);
  return localtime_r(&value, &out) != nullptr;
}

time_t addCalendarDays(time_t value, int days, bool utc,
                       const std::string& timezone) {
  if (utc) return value + static_cast<time_t>(days) * 86400;
  ScopedTimezone selectedTimezone(timezone);
  struct tm local = {};
  if (localtime_r(&value, &local) == nullptr) return 0;
  local.tm_mday += days;
  local.tm_isdst = -1;
  return mktime(&local);
}

time_t addCalendarMonths(time_t value, int months, bool utc,
                         const std::string& timezone) {
  struct tm original = {};
  if (!breakDown(value, utc, timezone, original)) return 0;
  const int targetMonthIndex = original.tm_year * 12 + original.tm_mon + months;
  const int targetYear = targetMonthIndex >= 0
                             ? targetMonthIndex / 12
                             : (targetMonthIndex - 11) / 12;
  const int targetMonth = targetMonthIndex - targetYear * 12;
  const int year = targetYear + 1900;
  const int month = targetMonth + 1;
  if (original.tm_mday > daysInMonth(year, month)) return 0;
  if (utc) {
    return local_time::utcEpochSeconds(
       year, month, original.tm_mday, original.tm_hour, original.tm_min,
       original.tm_sec);
  }
  ScopedTimezone selectedTimezone(timezone);
  struct tm candidate = original;
  candidate.tm_year = targetYear;
  candidate.tm_mon = targetMonth;
  candidate.tm_isdst = -1;
  const time_t result = mktime(&candidate);
  struct tm roundTrip = {};
  if (result <= 0 || localtime_r(&result, &roundTrip) == nullptr ||
      roundTrip.tm_year != targetYear || roundTrip.tm_mon != targetMonth ||
      roundTrip.tm_mday != original.tm_mday) {
    return 0;
  }
  return result;
}

time_t makeCalendarDate(const struct tm& original, int year, int month,
                       int day, bool utc, const std::string& timezone) {
  if (day < 1 || day > daysInMonth(year, month)) return 0;
  if (utc) {
    return local_time::utcEpochSeconds(
       year, month, day, original.tm_hour, original.tm_min, original.tm_sec);
  }
  ScopedTimezone selectedTimezone(timezone);
  struct tm candidate = original;
  candidate.tm_year = year - 1900;
  candidate.tm_mon = month - 1;
  candidate.tm_mday = day;
  candidate.tm_isdst = -1;
  const time_t result = mktime(&candidate);
  struct tm roundTrip = {};
  if (result <= 0 || localtime_r(&result, &roundTrip) == nullptr ||
      roundTrip.tm_year != year - 1900 ||
      roundTrip.tm_mon != month - 1 || roundTrip.tm_mday != day ||
      roundTrip.tm_hour != original.tm_hour ||
      roundTrip.tm_min != original.tm_min) {
    return 0;
  }
  return result;
}

bool parseInteger(const std::string& raw, int minimum, int maximum,
                 int& value) {
  const std::string text = trim(raw);
  if (text.empty()) return false;
  char* end = nullptr;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || parsed < minimum ||
      parsed > maximum) {
    return false;
  }
  value = static_cast<int>(parsed);
  return true;
}

int weekdayForCivil(int year, int month, int day) {
  int64_t weekday = (daysFromCivil(year, month, day) + 4) % 7;
  if (weekday < 0) weekday += 7;
  return static_cast<int>(weekday);
}

int resolvedMonthDay(int ruleDay, int year, int month) {
  const int count = daysInMonth(year, month);
  const int resolved = ruleDay > 0 ? ruleDay : count + ruleDay + 1;
  return resolved >= 1 && resolved <= count ? resolved : 0;
}

int ordinalWeekdayOfMonth(int year, int month, int weekday, int ordinal) {
  const int count = daysInMonth(year, month);
  if (ordinal > 0) {
    const int firstWeekday = weekdayForCivil(year, month, 1);
    const int day = 1 + (weekday - firstWeekday + 7) % 7 +
                   (ordinal - 1) * 7;
    return day <= count ? day : 0;
  }
  const int lastWeekday = weekdayForCivil(year, month, count);
  const int day =
      count - (lastWeekday - weekday + 7) % 7 + (ordinal + 1) * 7;
  return day >= 1 ? day : 0;
}

bool matchesMonthDay(const Rule& rule, int year, int month, int day) {
  if (rule.monthDays.empty()) return true;
  return std::any_of(
      rule.monthDays.begin(), rule.monthDays.end(), [&](int candidate) {
       return resolvedMonthDay(candidate, year, month) == day;
      });
}

bool matchesMonthlyWeekday(const Rule& rule, int year, int month, int day) {
  if (rule.weekdays == 0 && rule.ordinalWeekdays.empty()) return true;
  const int weekday = weekdayForCivil(year, month, day);
  if ((rule.weekdays & (1U << weekday)) != 0) return true;
  return std::any_of(
      rule.ordinalWeekdays.begin(), rule.ordinalWeekdays.end(),
      [&](const std::pair<int, int>& candidate) {
       return candidate.second == weekday &&
              ordinalWeekdayOfMonth(year, month, weekday,
                                    candidate.first) == day;
      });
}

bool parseRule(const RawEvent& event, Rule& result,
              std::string& failureReason) {
  result = Rule{};
  if (event.rule.empty()) return true;
  std::set<std::string> seen;
  for (const std::string& part : split(event.rule, ';')) {
    const size_t equals = part.find('=');
    if (equals == std::string::npos) {
      failureReason = "Invalid iCalendar RRULE component";
      return false;
    }
    const std::string key = upper(trim(part.substr(0, equals)));
    const std::string item = trim(part.substr(equals + 1));
    if (key.empty() || item.empty() || !seen.insert(key).second) {
      failureReason = "Invalid iCalendar RRULE component";
      return false;
    }
    if (key == "FREQ") {
      const std::string frequency = upper(item);
      if (frequency == "DAILY") result.frequency = Frequency::Daily;
      else if (frequency == "WEEKLY") result.frequency = Frequency::Weekly;
      else if (frequency == "MONTHLY") result.frequency = Frequency::Monthly;
      else if (frequency == "YEARLY") result.frequency = Frequency::Yearly;
      else {
       failureReason = "Unsupported iCalendar RRULE frequency";
       return false;
      }
    } else if (key == "INTERVAL") {
      if (!parseInteger(item, 1, 10000, result.interval)) {
       failureReason = "Invalid iCalendar RRULE interval";
       return false;
      }
    } else if (key == "COUNT") {
      if (!parseInteger(item, 1, 1000000, result.count)) {
       failureReason = "Invalid iCalendar RRULE count";
       return false;
      }
    } else if (key == "UNTIL") {
      bool untilDate = false;
      bool ignoredUtc = false;
      if (!parseDateTime(item, item.size() == 8, result.until, untilDate,
                        &ignoredUtc, event.timezone)) {
       failureReason = "Invalid iCalendar RRULE until value";
       return false;
      }
      if (untilDate) {
       const time_t nextDay =
           addCalendarDays(result.until, 1, false, event.timezone);
       if (nextDay <= result.until) {
         failureReason = "Invalid iCalendar RRULE until date";
         return false;
       }
       result.until = nextDay - 1;
      }
    } else if (key == "BYDAY") {
      for (const std::string& rawDay : split(item, ',')) {
       const std::string day = upper(trim(rawDay));
       const int weekday = weekdayFromToken(day);
       if (weekday < 0) {
         failureReason = "Invalid iCalendar RRULE weekday";
         return false;
       }
       const std::string prefix = day.substr(0, day.size() - 2);
       if (prefix.empty()) {
         result.weekdays |= static_cast<uint8_t>(1U << weekday);
       } else {
         int ordinal = 0;
         if (!parseInteger(prefix, -53, 53, ordinal) || ordinal == 0) {
           failureReason = "Invalid iCalendar RRULE weekday ordinal";
           return false;
         }
         result.ordinalWeekdays.push_back({ordinal, weekday});
       }
      }
    } else if (key == "BYMONTHDAY") {
      for (const std::string& rawDay : split(item, ',')) {
       int day = 0;
       if (!parseInteger(rawDay, -31, 31, day) || day == 0) {
         failureReason = "Invalid iCalendar RRULE month day";
         return false;
       }
       result.monthDays.push_back(day);
      }
    } else if (key == "WKST") {
      const int weekday = weekdayFromToken(item);
      if (item.size() != 2 || weekday < 0) {
       failureReason = "Invalid iCalendar RRULE week start";
       return false;
      }
      result.weekStart = weekday;
    } else {
      failureReason = "Unsupported iCalendar RRULE selector: " + key;
      return false;
    }
  }
  if (result.frequency == Frequency::None) {
    failureReason = "iCalendar RRULE is missing FREQ";
    return false;
  }
  if (!result.ordinalWeekdays.empty() &&
      result.frequency != Frequency::Monthly) {
    failureReason =
       "Ordinal BYDAY is supported only for monthly iCalendar rules";
    return false;
  }
  if (result.frequency == Frequency::Yearly &&
      (result.weekdays != 0 || !result.ordinalWeekdays.empty() ||
      !result.monthDays.empty())) {
    failureReason =
       "BYDAY and BYMONTHDAY are not supported for yearly iCalendar rules";
    return false;
  }
  return true;
}

bool isExcluded(const RawEvent& event, time_t occurrence) {
  return std::find(event.excluded.begin(), event.excluded.end(), occurrence) !=
         event.excluded.end();
}

const RawEvent* overrideFor(const std::vector<RawEvent>& events,
                            const RawEvent& master, time_t occurrence) {
  for (const RawEvent& candidate : events) {
    if (candidate.recurrenceId == occurrence &&
        !candidate.uid.empty() && candidate.uid == master.uid) {
      return &candidate;
    }
  }
  return nullptr;
}

std::string overrideKey(const RawEvent& event) {
  return event.uid + "\n" +
         std::to_string(static_cast<long long>(event.recurrenceId));
}

void addOccurrence(const RawEvent& raw, time_t start, time_t end,
                   uint32_t defaultColor, const calendar::Window& window,
                   size_t maximumEvents, calendar::Data& out) {
  if (raw.cancelled || start <= 0 || end <= start ||
      start >= window.end || end <= window.start) {
    return;
  }
  if (out.events.size() >= maximumEvents) {
    out.truncated = true;
    return;
  }
  calendar::Event event;
  event.uid = raw.uid;
  event.title = raw.title.empty() ? "(untitled)" : raw.title;
  event.location = raw.location;
  event.start = start;
  event.end = end;
  event.allDay = raw.allDay;
  event.colorRgb = raw.haveColor ? raw.color : defaultColor;
  out.events.push_back(std::move(event));
}

void expandMaster(const RawEvent& master, const Rule& rule,
                  const std::vector<RawEvent>& allEvents,
                  uint32_t defaultColor, const calendar::Window& window,
                  size_t maximumEvents,
                  std::set<std::string>& handledOverrides,
                  calendar::Data& out) {
  const time_t fixedDuration =
      master.haveEnd && master.end > master.start
          ? master.end - master.start
          : (master.allDay ? 86400 : 3600);
  int allDayDuration = 1;
  if (master.allDay && master.haveEnd) {
    struct tm startLocal = {};
    struct tm endLocal = {};
    if (breakDown(master.start, false, master.timezone, startLocal) &&
        breakDown(master.end, false, master.timezone, endLocal)) {
      allDayDuration = static_cast<int>(
          daysFromCivil(endLocal.tm_year + 1900, endLocal.tm_mon + 1,
                        endLocal.tm_mday) -
          daysFromCivil(startLocal.tm_year + 1900, startLocal.tm_mon + 1,
                        startLocal.tm_mday));
      allDayDuration = std::max(1, allDayDuration);
    }
  }
  int emittedByRule = 0;

  auto occurrenceEnd = [&](time_t occurrence, bool allDay) {
    if (master.haveDuration) {
      const time_t dayAdjusted =
          addCalendarDays(occurrence, master.durationDays, master.utc,
                          master.timezone);
      if (dayAdjusted <= 0 ||
          master.durationSeconds >
              std::numeric_limits<time_t>::max() - dayAdjusted) {
        return static_cast<time_t>(0);
      }
      return dayAdjusted + master.durationSeconds;
    }
    return allDay
               ? addCalendarDays(occurrence, allDayDuration, false,
                                master.timezone)
               : occurrence + fixedDuration;
  };

  auto consider = [&](time_t occurrence) {
    if (occurrence <= 0) return;
    ++emittedByRule;
    if (rule.until > 0 && occurrence > rule.until) return;
    const RawEvent* replacement = overrideFor(allEvents, master, occurrence);
    if (replacement != nullptr) {
      handledOverrides.insert(overrideKey(*replacement));
    }
    if (isExcluded(master, occurrence)) return;
    if (replacement != nullptr) {
      if (!replacement->cancelled) {
        const time_t replacementEnd =
            replacement->haveEnd && replacement->end > replacement->start
                ? replacement->end
                : occurrenceEnd(replacement->start, replacement->allDay);
        const uint32_t replacementDefault =
            master.haveColor ? master.color : defaultColor;
        addOccurrence(*replacement, replacement->start, replacementEnd,
                      replacementDefault, window, maximumEvents, out);
      }
      return;
    }
    addOccurrence(master, occurrence, occurrenceEnd(occurrence, master.allDay),
                  defaultColor,
                  window, maximumEvents, out);
  };

  consider(master.start);
  if (out.truncated) return;
  if (rule.frequency == Frequency::None ||
      (rule.count > 0 && emittedByRule >= rule.count)) {
    return;
  }

  constexpr int kMaximumGeneratedOccurrences = 32768;
  int attempts = 0;
  if (rule.frequency == Frequency::Daily) {
    int firstDay = rule.interval;
    if (rule.count == 0 && window.start > master.start) {
      const int approximateDays = static_cast<int>(
          (window.start - master.start) / 86400) - 2;
      if (approximateDays > firstDay) {
        firstDay =
            std::max(rule.interval,
                     (approximateDays / rule.interval) * rule.interval);
      }
    }
    for (int day = firstDay; attempts < kMaximumGeneratedOccurrences;
         day += rule.interval, ++attempts) {
      const time_t occurrence =
          addCalendarDays(master.start, day, master.utc, master.timezone);
      if (occurrence <= master.start) break;
      if (rule.until > 0 && occurrence > rule.until) break;
      if (occurrence >= window.end) break;
      struct tm local = {};
      if (!breakDown(occurrence, master.utc, master.timezone, local)) break;
      if ((rule.weekdays != 0 &&
           (rule.weekdays & (1U << local.tm_wday)) == 0) ||
          !matchesMonthDay(rule, local.tm_year + 1900, local.tm_mon + 1,
                           local.tm_mday)) {
        continue;
      }
      consider(occurrence);
      if (out.truncated) break;
      if (rule.count > 0 && emittedByRule >= rule.count) break;
    }
  } else if (rule.frequency == Frequency::Weekly) {
    struct tm startLocal = {};
    if (!breakDown(master.start, master.utc, master.timezone, startLocal)) {
      return;
    }
    uint8_t weekdays = rule.weekdays;
    if (weekdays == 0) weekdays = static_cast<uint8_t>(1U << startLocal.tm_wday);
    const int daysFromWeekStart =
        (startLocal.tm_wday - rule.weekStart + 7) % 7;
    int firstDay = 1;
    if (rule.count == 0 && window.start > master.start) {
      const int approximateDays = static_cast<int>(
          (window.start - master.start) / 86400) -
          (7 * rule.interval + 7);
      firstDay = std::max(1, approximateDays);
    }
    for (int day = firstDay; attempts < kMaximumGeneratedOccurrences;
         ++day, ++attempts) {
      const time_t occurrence =
          addCalendarDays(master.start, day, master.utc, master.timezone);
      if (occurrence <= master.start) break;
      if (rule.until > 0 && occurrence > rule.until) break;
      if (occurrence >= window.end) break;
      struct tm local = {};
      if (!breakDown(occurrence, master.utc, master.timezone, local)) break;
      const int weekIndex = (daysFromWeekStart + day) / 7;
      if ((weekdays & (1U << local.tm_wday)) == 0 ||
          (weekIndex % rule.interval) != 0 ||
          !matchesMonthDay(rule, local.tm_year + 1900, local.tm_mon + 1,
                           local.tm_mday)) {
        continue;
      }
      consider(occurrence);
      if (out.truncated) break;
      if (rule.count > 0 && emittedByRule >= rule.count) break;
    }
  } else if (rule.frequency == Frequency::Monthly) {
    struct tm original = {};
    if (!breakDown(master.start, master.utc, master.timezone, original)) return;
    for (int period = 0; attempts < kMaximumGeneratedOccurrences;
         period += rule.interval, ++attempts) {
      const int monthIndex =
          original.tm_year * 12 + original.tm_mon + period;
      const int targetYearIndex =
          monthIndex >= 0 ? monthIndex / 12 : (monthIndex - 11) / 12;
      const int year = targetYearIndex + 1900;
      const int month = monthIndex - targetYearIndex * 12 + 1;
      std::set<int> days;
      if (!rule.monthDays.empty()) {
        for (const int monthDay : rule.monthDays) {
          const int resolved = resolvedMonthDay(monthDay, year, month);
          if (resolved > 0) days.insert(resolved);
        }
      } else if (rule.weekdays != 0 ||
                 !rule.ordinalWeekdays.empty()) {
        for (int day = 1; day <= daysInMonth(year, month); ++day) {
          if (matchesMonthlyWeekday(rule, year, month, day)) days.insert(day);
        }
      } else {
        days.insert(original.tm_mday);
      }
      bool beyondWindow = false;
      for (const int day : days) {
        if (!matchesMonthlyWeekday(rule, year, month, day)) continue;
        const time_t occurrence =
            makeCalendarDate(original, year, month, day, master.utc,
                             master.timezone);
        if (occurrence <= master.start) continue;
        if (rule.until > 0 && occurrence > rule.until) {
          beyondWindow = true;
          break;
        }
        if (occurrence >= window.end) {
          beyondWindow = true;
          break;
        }
        consider(occurrence);
        if (out.truncated ||
            (rule.count > 0 && emittedByRule >= rule.count)) {
          return;
        }
      }
      if (beyondWindow) break;
    }
  } else {
    for (int period = rule.interval;
         attempts < kMaximumGeneratedOccurrences;
         period += rule.interval, ++attempts) {
      const time_t occurrence =
          addCalendarMonths(master.start, period * 12, master.utc,
                            master.timezone);
      if (occurrence == 0) continue;
      if (rule.until > 0 && occurrence > rule.until) break;
      if (occurrence >= window.end) break;
      consider(occurrence);
      if (out.truncated) break;
      if (rule.count > 0 && emittedByRule >= rule.count) break;
    }
  }
  if (attempts >= kMaximumGeneratedOccurrences) out.truncated = true;
}

std::string generatedUid(const RawEvent& event) {
  char timestamp[32] = {};
  std::snprintf(timestamp, sizeof(timestamp), "-%lld",
                static_cast<long long>(event.start));
  return event.title + timestamp;
}

}  // namespace

bool parse(const std::string& payload, const calendar::Window& window,
           size_t maximumEvents, calendar::Data& out,
           std::string& failureReason) {
  out = calendar::Data{};
  failureReason.clear();
  if (payload.empty()) {
    failureReason = "Calendar response is empty";
    return false;
  }
  if (window.start <= 0 || window.end <= window.start) {
    failureReason = "Calendar display window is invalid";
    return false;
  }

  std::vector<RawEvent> rawEvents;
  RawEvent current;
  bool inCalendar = false;
  bool inEvent = false;
  bool sawCalendar = false;
  bool closedCalendar = false;
  bool malformedStructure = false;
  std::vector<std::string> nestedCalendarComponents;
  std::vector<std::string> nestedEventComponents;
  std::string calendarName = "iCalendar";
  uint32_t calendarColor = 0x4A6FA5;

  for (const std::string& line : unfold(payload)) {
    ParsedProperty property;
    if (!parseProperty(line, property)) continue;
    if (property.name == "BEGIN" && upper(property.value) == "VCALENDAR") {
      if (inCalendar || sawCalendar) {
        malformedStructure = true;
        break;
      }
      sawCalendar = true;
      inCalendar = true;
      continue;
    }
    if (property.name == "END" && upper(property.value) == "VCALENDAR") {
      if (!inCalendar || inEvent || !nestedCalendarComponents.empty()) {
        malformedStructure = true;
        break;
      }
      inCalendar = false;
      closedCalendar = true;
      continue;
    }
    if (!inCalendar) continue;
    const std::string componentName = upper(trim(property.value));
    if (!inEvent && property.name == "BEGIN" &&
        componentName != "VEVENT") {
      if (componentName.empty()) {
        malformedStructure = true;
        break;
      }
      nestedCalendarComponents.push_back(componentName);
      continue;
    }
    if (!inEvent && property.name == "END" &&
        componentName != "VCALENDAR") {
      if (nestedCalendarComponents.empty() ||
          nestedCalendarComponents.back() != componentName) {
        malformedStructure = true;
        break;
      }
      nestedCalendarComponents.pop_back();
      continue;
    }
    if (!inEvent && !nestedCalendarComponents.empty()) continue;
    if (inEvent && property.name == "BEGIN") {
      if (componentName.empty() || componentName == "VEVENT") {
        malformedStructure = true;
        break;
      }
      nestedEventComponents.push_back(componentName);
      continue;
    }
    if (inEvent && property.name == "END" &&
        !nestedEventComponents.empty()) {
      if (nestedEventComponents.back() != componentName) {
        malformedStructure = true;
        break;
      }
      nestedEventComponents.pop_back();
      continue;
    }
    if (inEvent && property.name == "END" && componentName != "VEVENT") {
      malformedStructure = true;
      break;
    }
    if (property.name == "BEGIN" && upper(property.value) == "VEVENT") {
      if (inEvent) {
        malformedStructure = true;
        break;
      }
      current = RawEvent{};
      nestedEventComponents.clear();
      inEvent = true;
      continue;
    }
    if (property.name == "END" && upper(property.value) == "VEVENT") {
      if (!inEvent) {
        malformedStructure = true;
        break;
      }
      const bool cancelledOverride =
          current.cancelled && current.recurrenceId != 0;
      if (inEvent && (current.haveStart || cancelledOverride)) {
        if (current.haveEnd && current.haveDuration) {
          failureReason =
              "iCalendar event cannot contain both DTEND and DURATION";
          return false;
        }
        if (current.haveStart && !current.haveEnd) {
          if (current.haveDuration) {
            if (current.allDay &&
                (current.durationDays <= 0 ||
                 current.durationSeconds != 0)) {
              failureReason =
                  "All-day iCalendar duration must use complete days";
              return false;
            }
            const time_t dayAdjusted = addCalendarDays(
                current.start, current.durationDays, current.utc,
                current.timezone);
            if (dayAdjusted <= 0 ||
                current.durationSeconds >
                    std::numeric_limits<time_t>::max() - dayAdjusted) {
              failureReason = "iCalendar DURATION exceeds the time range";
              return false;
            }
            current.end = dayAdjusted + current.durationSeconds;
          } else {
            current.end =
                current.allDay
                    ? addCalendarDays(current.start, 1, false,
                                      current.timezone)
                    : current.start + 3600;
          }
          current.haveEnd = current.end > current.start;
        }
        if (current.uid.empty()) current.uid = generatedUid(current);
        rawEvents.push_back(std::move(current));
      }
      current = RawEvent{};
      nestedEventComponents.clear();
      inEvent = false;
      continue;
    }

    if (!nestedEventComponents.empty()) continue;
    if (!inEvent) {
      if (property.name == "X-WR-CALNAME" || property.name == "NAME") {
        calendarName = unescapeText(property.value);
      } else if (property.name == "COLOR" ||
                 property.name == "X-APPLE-CALENDAR-COLOR") {
        calendarColor = parseColor(property.value, calendarColor);
      }
      continue;
    }

    if (property.name == "UID") {
      current.uid = trim(property.value);
    } else if (property.name == "SUMMARY") {
      current.title = unescapeText(property.value);
    } else if (property.name == "LOCATION") {
      current.location = unescapeText(property.value);
    } else if (property.name == "DTSTART") {
      current.haveStart = parseTemporalProperty(
          property, std::string(), current.start, current.allDay,
          &current.utc, &current.timezone, failureReason);
      if (!current.haveStart) return false;
    } else if (property.name == "DTEND") {
      bool endAllDay = false;
      bool ignoredUtc = false;
      current.haveEnd = parseTemporalProperty(
          property, current.timezone, current.end, endAllDay, &ignoredUtc,
          nullptr, failureReason);
      if (!current.haveEnd) return false;
      if (current.haveStart && endAllDay != current.allDay) {
        failureReason = "iCalendar DTSTART and DTEND value types do not match";
        return false;
      }
    } else if (property.name == "DURATION") {
      current.haveDuration =
          parseDuration(property.value, current.durationDays,
                        current.durationSeconds);
      if (!current.haveDuration) {
        failureReason = "Invalid iCalendar DURATION value";
        return false;
      }
    } else if (property.name == "RECURRENCE-ID") {
      bool ignoredAllDay = false;
      bool ignoredUtc = false;
      if (!parseTemporalProperty(
              property, current.timezone, current.recurrenceId,
              ignoredAllDay, &ignoredUtc, nullptr, failureReason)) {
        return false;
      }
    } else if (property.name == "RRULE") {
      current.rule = property.value;
    } else if (property.name == "EXDATE") {
      for (const std::string& excludedValue : split(property.value, ',')) {
        ParsedProperty excludedProperty = property;
        excludedProperty.value = excludedValue;
        time_t excluded = 0;
        bool ignoredAllDay = false;
        bool ignoredUtc = false;
        if (!parseTemporalProperty(
                excludedProperty, current.timezone, excluded, ignoredAllDay,
                &ignoredUtc, nullptr, failureReason)) {
          return false;
        }
        current.excluded.push_back(excluded);
      }
    } else if (property.name == "STATUS") {
      current.cancelled = upper(trim(property.value)) == "CANCELLED";
    } else if (property.name == "COLOR" ||
               property.name == "X-APPLE-CALENDAR-COLOR") {
      current.color = parseColor(property.value, calendarColor);
      current.haveColor = true;
    }
  }

  if (!sawCalendar) {
    failureReason = "Response is not an iCalendar document";
    return false;
  }
  if (malformedStructure || inCalendar || inEvent || !closedCalendar) {
    failureReason = "iCalendar document is truncated or structurally invalid";
    return false;
  }

  calendar::Source source;
  source.id = "ical";
  source.name = calendarName.empty() ? "iCalendar" : calendarName;
  source.colorRgb = calendarColor;
  out.sources.push_back(source);
  out.sourceLabel = source.name;

  if (rawEvents.empty()) return true;

  std::set<std::string> handledOverrides;
  for (const RawEvent& event : rawEvents) {
    if (event.recurrenceId != 0) continue;
    Rule rule;
    if (!parseRule(event, rule, failureReason)) return false;
    expandMaster(event, rule, rawEvents, calendarColor, window,
                 maximumEvents, handledOverrides, out);
    if (out.truncated) break;
  }

  if (!out.truncated) {
    for (const RawEvent& override : rawEvents) {
      if (override.recurrenceId == 0 || override.cancelled ||
          !override.haveStart ||
          handledOverrides.count(overrideKey(override)) != 0) {
        continue;
      }
      const auto master =
          std::find_if(rawEvents.begin(), rawEvents.end(),
                       [&](const RawEvent& candidate) {
                         return candidate.recurrenceId == 0 &&
                                candidate.uid == override.uid;
                       });
      const uint32_t fallback =
          master != rawEvents.end() && master->haveColor
              ? master->color
              : calendarColor;
      addOccurrence(override, override.start, override.end, fallback, window,
                    maximumEvents, out);
      if (out.truncated) break;
    }
  }

  std::sort(out.events.begin(), out.events.end(),
            [](const calendar::Event& left, const calendar::Event& right) {
              if (left.start != right.start) return left.start < right.start;
              if (left.allDay != right.allDay) return left.allDay;
              if (left.end != right.end) return left.end < right.end;
              return left.title < right.title;
            });
  return true;
}

}  // namespace ical
