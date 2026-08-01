#pragma once

#include <Arduino.h>
#include <math.h>
#include <time.h>

#include "config.h"

struct DailyForecast {
  // Calendar date at the observation location, formatted "YYYY-MM-DD".
  // A calendar day has no timezone semantics -- keeping this as a
  // string sidesteps the "what day is Aug 2 at UTC+8 vs UTC+1?"
  // question that would arise if we stored an epoch here.
  String date;
  int weatherCode = -1;
  float minimumC = NAN;
  float maximumC = NAN;
  float uvMaximum = NAN;
  int precipitationProbability = -1;
};

struct WeatherData {
  bool valid = false;
  bool fromCache = false;
  bool rainTimingAvailable = false;
  bool rainExpected = false;
  // All timestamps below are UTC epoch seconds. 0 means "unknown / not
  // set". Convert to device-local wall clock only at the render / log
  // boundary using localtime_r(). Rationale: see plan.md "Store all
  // internal clocks in UTC epoch" -- ensures a device timezone change
  // never silently reinterprets stored data.
  time_t updateTime = 0;
  time_t nextRainTime = 0;
  float temperatureC = NAN;
  float apparentC = NAN;
  float humidityPct = NAN;
  float windKmh = NAN;
  float nextRainMm = NAN;
  int nextRainProbability = -1;
  int weatherCode = -1;
  bool isDay = true;
  // Provider-supplied severe weather alert. alertTitle is empty when no
  // alert is active or the provider does not expose alerts. alertOtherCount
  // is the number of *additional* alerts beyond the one we display.
  String alertTitle;
  String alertSeverity;
  int alertOtherCount = 0;
  DailyForecast days[config::FORECAST_DAYS];
};
