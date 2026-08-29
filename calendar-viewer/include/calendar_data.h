#pragma once

#include <stdint.h>
#include <time.h>

#include <string>
#include <vector>

namespace calendar {

struct Source {
  std::string id;
  std::string name;
  uint32_t colorRgb = 0x4A6FA5;
};

struct Event {
  std::string uid;
  std::string title;
  std::string location;
  time_t start = 0;
  time_t end = 0;
  bool allDay = false;
  uint32_t colorRgb = 0x4A6FA5;
  uint8_t sourceIndex = 0;
};

struct Data {
  std::vector<Source> sources;
  std::vector<Event> events;
  std::string sourceLabel;
  time_t fetchedAt = 0;
  bool truncated = false;
};

struct Window {
  time_t start = 0;
  time_t end = 0;
};

}  // namespace calendar
