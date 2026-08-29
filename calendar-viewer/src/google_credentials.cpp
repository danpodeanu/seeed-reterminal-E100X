#include "google_credentials.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "calendar_config_schema.h"

namespace google_credentials {
namespace {

struct CredentialKeys {
  const char* project;
  const char* privateKeyId;
  const char* privateKey;
  const char* clientEmail;
  const char* tokenUri;
};

constexpr const char* kActiveSlotKey = "g_active";
constexpr uint8_t kNoActiveSlot = 0xFF;
constexpr CredentialKeys kSlots[] = {
    {"g0_project", "g0_key_id", "g0_priv_key", "g0_email", "g0_token_uri"},
    {"g1_project", "g1_key_id", "g1_priv_key", "g1_email", "g1_token_uri"},
};

constexpr const char* kLegacyValidKey = "g_valid";
constexpr CredentialKeys kLegacyKeys = {
    "g_project", "g_key_id", "g_priv_key", "g_email", "g_token_uri"};

bool readString(Preferences& prefs, const char* key, String& out) {
  out = prefs.getString(key, "");
  return !out.isEmpty();
}

void scrub(String& value) {
  for (size_t i = 0; i < value.length(); ++i) value.setCharAt(i, '\0');
  value = "";
}

bool validEmail(const String& value) {
  const int at = value.indexOf('@');
  return at > 0 && value.endsWith(".gserviceaccount.com");
}

bool validPrivateKey(const String& value) {
  return value.indexOf("-----BEGIN PRIVATE KEY-----") >= 0 &&
         value.indexOf("-----END PRIVATE KEY-----") >= 0 &&
         value.length() < 4096;
}

bool validCredentials(const Credentials& value) {
  return !value.projectId.isEmpty() && !value.privateKeyId.isEmpty() &&
         validEmail(value.clientEmail) && validPrivateKey(value.privateKey) &&
         value.tokenUri == "https://oauth2.googleapis.com/token";
}

bool readCredential(Preferences& prefs, const CredentialKeys& keys,
                    Credentials& out) {
  out = Credentials{};
  return readString(prefs, keys.project, out.projectId) &&
         readString(prefs, keys.privateKeyId, out.privateKeyId) &&
         readString(prefs, keys.privateKey, out.privateKey) &&
         readString(prefs, keys.clientEmail, out.clientEmail) &&
         readString(prefs, keys.tokenUri, out.tokenUri);
}

bool sameCredential(const Credentials& left, const Credentials& right) {
  return left.projectId == right.projectId &&
         left.privateKeyId == right.privateKeyId &&
         left.privateKey == right.privateKey &&
         left.clientEmail == right.clientEmail &&
         left.tokenUri == right.tokenUri;
}

bool removeIfPresent(Preferences& prefs, const char* key) {
  return !prefs.isKey(key) || prefs.remove(key);
}

bool removeCredential(Preferences& prefs, const CredentialKeys& keys) {
  bool removed = true;
  removed = removeIfPresent(prefs, keys.project) && removed;
  removed = removeIfPresent(prefs, keys.privateKeyId) && removed;
  removed = removeIfPresent(prefs, keys.privateKey) && removed;
  removed = removeIfPresent(prefs, keys.clientEmail) && removed;
  removed = removeIfPresent(prefs, keys.tokenUri) && removed;
  return removed;
}

bool wipeAndRemovePrivateKey(Preferences& prefs, const char* key) {
  if (!prefs.isKey(key)) return true;
  String oldKey = prefs.getString(key, "");
  bool wiped = true;
  if (!oldKey.isEmpty()) {
    String zeros;
    if (!zeros.reserve(oldKey.length())) {
      wiped = false;
    } else {
      for (size_t i = 0; i < oldKey.length(); ++i) zeros += '0';
      wiped = prefs.putString(key, zeros) == zeros.length();
      scrub(zeros);
    }
  }
  scrub(oldKey);
  return removeIfPresent(prefs, key) && wiped;
}

bool wipeAndRemoveCredential(Preferences& prefs,
                             const CredentialKeys& keys) {
  bool removed = wipeAndRemovePrivateKey(prefs, keys.privateKey);
  removed = removeIfPresent(prefs, keys.project) && removed;
  removed = removeIfPresent(prefs, keys.privateKeyId) && removed;
  removed = removeIfPresent(prefs, keys.clientEmail) && removed;
  removed = removeIfPresent(prefs, keys.tokenUri) && removed;
  return removed;
}

}  // namespace

bool load(Credentials& out, String& failureReason) {
  out = Credentials{};
  failureReason = "";
  Preferences prefs;
  if (!prefs.begin(calendar_config::kNamespace, true)) {
    failureReason = "Could not open credential storage";
    return false;
  }
  const uint8_t activeSlot =
      prefs.getUChar(kActiveSlotKey, kNoActiveSlot);
  bool complete = false;
  if (activeSlot < sizeof(kSlots) / sizeof(kSlots[0])) {
    complete = readCredential(prefs, kSlots[activeSlot], out);
  } else if (activeSlot == kNoActiveSlot &&
             prefs.getBool(kLegacyValidKey, false)) {
    complete = readCredential(prefs, kLegacyKeys, out);
  }
  prefs.end();
  if (!complete || !validCredentials(out)) {
    scrub(out.privateKey);
    out = Credentials{};
    failureReason = "Google service-account credentials are not configured";
    return false;
  }
  return true;
}

bool storeJson(const String& json, String& failureReason) {
  failureReason = "";
  if (json.isEmpty()) {
    failureReason = "The uploaded file is empty";
    return false;
  }

  JsonDocument document;
  const DeserializationError parseError = deserializeJson(document, json);
  if (parseError) {
    failureReason = String("Invalid JSON: ") + parseError.c_str();
    return false;
  }

  const String type = String(document["type"] | "");
  Credentials candidate;
  candidate.projectId = String(document["project_id"] | "");
  candidate.privateKeyId = String(document["private_key_id"] | "");
  candidate.privateKey = String(document["private_key"] | "");
  candidate.clientEmail = String(document["client_email"] | "");
  candidate.tokenUri = String(document["token_uri"] | "");
  document.clear();

  if (type != "service_account") {
    scrub(candidate.privateKey);
    failureReason = "JSON type must be service_account";
    return false;
  }
  if (candidate.projectId.isEmpty() || candidate.privateKeyId.isEmpty() ||
      !validEmail(candidate.clientEmail) ||
      !validPrivateKey(candidate.privateKey)) {
    scrub(candidate.privateKey);
    failureReason = "The service-account JSON is missing required IAM fields";
    return false;
  }
  if (candidate.tokenUri != "https://oauth2.googleapis.com/token") {
    scrub(candidate.privateKey);
    failureReason = "The service-account token_uri is not Google's HTTPS endpoint";
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(calendar_config::kNamespace, false)) {
    scrub(candidate.privateKey);
    failureReason = "Could not open credential storage";
    return false;
  }
  const uint8_t activeSlot =
      prefs.getUChar(kActiveSlotKey, kNoActiveSlot);
  const uint8_t targetSlot = activeSlot == 0 ? 1 : 0;
  const CredentialKeys& target = kSlots[targetSlot];
  bool saved = removeCredential(prefs, target);
  saved = saved &&
          prefs.putString(target.project, candidate.projectId) > 0 &&
          prefs.putString(target.privateKeyId, candidate.privateKeyId) > 0 &&
          prefs.putString(target.privateKey, candidate.privateKey) > 0 &&
          prefs.putString(target.clientEmail, candidate.clientEmail) > 0 &&
          prefs.putString(target.tokenUri, candidate.tokenUri) > 0;
  Credentials verification;
  if (saved) {
    saved = readCredential(prefs, target, verification) &&
            validCredentials(verification) &&
            sameCredential(candidate, verification);
  }
  scrub(verification.privateKey);
  if (saved) saved = prefs.putUChar(kActiveSlotKey, targetSlot) > 0;
  if (saved) {
    wipeAndRemoveCredential(prefs, kLegacyKeys);
    removeIfPresent(prefs, kLegacyValidKey);
  } else {
    wipeAndRemoveCredential(prefs, target);
  }
  prefs.end();
  scrub(candidate.privateKey);
  if (!saved) {
    failureReason = "NVS did not accept the complete credential";
    return false;
  }
  return true;
}

bool configured() {
  Credentials credentials;
  String ignored;
  const bool result = load(credentials, ignored);
  scrub(credentials.privateKey);
  return result;
}

String configuredEmail() {
  Credentials credentials;
  String ignored;
  if (!load(credentials, ignored)) return "";
  const String result = credentials.clientEmail;
  scrub(credentials.privateKey);
  return result;
}

bool clear(String& failureReason) {
  failureReason = "";
  Preferences prefs;
  if (!prefs.begin(calendar_config::kNamespace, false)) {
    failureReason = "Could not open credential storage";
    return false;
  }
  bool cleared = prefs.putUChar(kActiveSlotKey, kNoActiveSlot) > 0;
  cleared = (prefs.putBool(kLegacyValidKey, false) > 0) && cleared;
  for (const CredentialKeys& slot : kSlots) {
    cleared = wipeAndRemovePrivateKey(prefs, slot.privateKey) && cleared;
    cleared = removeIfPresent(prefs, slot.project) && cleared;
    cleared = removeIfPresent(prefs, slot.privateKeyId) && cleared;
    cleared = removeIfPresent(prefs, slot.clientEmail) && cleared;
    cleared = removeIfPresent(prefs, slot.tokenUri) && cleared;
  }
  cleared = wipeAndRemovePrivateKey(prefs, kLegacyKeys.privateKey) && cleared;
  cleared = removeIfPresent(prefs, kLegacyKeys.project) && cleared;
  cleared = removeIfPresent(prefs, kLegacyKeys.privateKeyId) && cleared;
  cleared = removeIfPresent(prefs, kLegacyKeys.clientEmail) && cleared;
  cleared = removeIfPresent(prefs, kLegacyKeys.tokenUri) && cleared;
  cleared = removeIfPresent(prefs, kLegacyValidKey) && cleared;
  cleared = removeIfPresent(prefs, kActiveSlotKey) && cleared;
  prefs.end();
  if (!cleared) {
    failureReason = "Could not completely remove the stored credential";
    return false;
  }
  return true;
}

}  // namespace google_credentials
