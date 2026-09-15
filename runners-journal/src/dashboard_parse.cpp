#include "dashboard_parse.h"

#include <ArduinoJson.h>

namespace dashboard {

namespace {

void parseWeek(const JsonObject& o, WeekSummary& w) {
  w.merkelapp = o["merkelapp"] | "";
  w.total_km = o["total_km"] | 0.0f;
  w.antall = o["antall"] | 0;
  w.maal_pct = o["maal_pct"] | 0;
  w.mot_forrige_km = o["mot_forrige_km"] | "";
}

void parseHistorikk(const JsonArray& arr, std::vector<HistoryEntry>& out) {
  out.clear();
  out.reserve(arr.size());
  for (const JsonVariant& v : arr) {
    const JsonObject& o = v.as<JsonObject>();
    HistoryEntry e;
    e.uke = o["uke"] | "";
    e.km = o["km"] | 0.0f;
    out.push_back(std::move(e));
  }
}

void parseTyper(const JsonArray& arr, std::vector<TypeEntry>& out) {
  out.clear();
  out.reserve(arr.size());
  for (const JsonVariant& v : arr) {
    const JsonObject& o = v.as<JsonObject>();
    TypeEntry e;
    e.type = o["type"] | "";
    e.antall = o["antall"] | 0;
    e.km = o["km"] | 0.0f;
    out.push_back(std::move(e));
  }
}

void parseSisteLop(const JsonArray& arr, std::vector<RunEntry>& out) {
  out.clear();
  out.reserve(arr.size());
  for (const JsonVariant& v : arr) {
    const JsonObject& o = v.as<JsonObject>();
    RunEntry e;
    e.dato = o["dato"] | "";
    e.type = o["type"] | "";
    e.km = o["km"] | 0.0f;
    e.pace = o["pace"] | "";
    out.push_back(std::move(e));
  }
}

void parseJournal(const JsonArray& arr, std::vector<JournalEntry>& out) {
  out.clear();
  out.reserve(arr.size());
  for (const JsonVariant& v : arr) {
    const JsonObject& o = v.as<JsonObject>();
    JournalEntry e;
    e.dato = o["dato"] | "";
    e.type = o["type"] | "";
    e.note = o["note"] | "";
    out.push_back(std::move(e));
  }
}

}  // namespace

bool parse(const String& body, DashboardData& out) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err != DeserializationError::Ok) {
    return false;
  }
  const JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) {
    return false;
  }

  out.oppdatert = root["oppdatert"] | "";
  out.neste_oppvakning_s = root["neste_oppvakning_s"] | 0u;
  out.maal_km = root["maal_km"] | "";

  const JsonObject ukeObj = root["uke"];
  if (!ukeObj.isNull()) parseWeek(ukeObj, out.uke);

  const JsonArray historikk = root["historikk"];
  if (!historikk.isNull()) parseHistorikk(historikk, out.historikk);

  const JsonArray typer = root["typer"];
  if (!typer.isNull()) parseTyper(typer, out.typer);

  const JsonArray sisteLop = root["siste_lop"];
  if (!sisteLop.isNull()) parseSisteLop(sisteLop, out.siste_lop);

  const JsonArray journal = root["journal"];
  if (!journal.isNull()) parseJournal(journal, out.journal);

  return true;
}

}  // namespace dashboard
