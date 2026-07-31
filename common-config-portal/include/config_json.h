#pragma once

#include "config_portal_string_shim.h"
#include "config_schema.h"

#include <map>
#include <utility>
#include <vector>

namespace config_portal {
namespace json {

String valuesToJson(const std::vector<std::pair<String, String>>& values);
bool parseSubmissionJson(const Schema& schema, const String& body,
                         std::map<String, String>& out, String* err);
String errorJson(const char* message);

}  // namespace json
}  // namespace config_portal
