#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <vector>

namespace dashboard {

struct WeekSummary {
  String merkelapp;
  float total_km = 0.0f;
  int antall = 0;
  int maal_pct = 0;
  String mot_forrige_km;
  uint32_t total_tid_s = 0;
  String total_tid;
  int elevation_m = 0;
};

struct YearSummary {
  float total_km = 0.0f;
};

struct HistoryEntry {
  String uke;
  float km = 0.0f;
  bool naa = false;
};

struct TypeEntry {
  String type;
  int antall = 0;
  float km = 0.0f;
};

struct RunEntry {
  String dato;
  String type;
  float km = 0.0f;
  String pace;
  int elevation_m = 0;
};

struct JournalEntry {
  String dato;
  String type;
  String note;
};

struct DashboardData {
  String oppdatert;
  uint32_t neste_oppvakning_s = 0;
  String maal_km;
  WeekSummary uke;
  YearSummary aar;
  std::vector<HistoryEntry> historikk;
  std::vector<TypeEntry> typer;
  std::vector<RunEntry> siste_lop;
  std::vector<JournalEntry> journal;
};

}  // namespace dashboard
