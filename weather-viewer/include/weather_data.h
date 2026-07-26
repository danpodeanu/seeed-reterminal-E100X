#pragma once

#include <Arduino.h>
#include <math.h>

#include "config.h"

struct DailyForecast {
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
  String updateTime;
  String nextRainTime;
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
