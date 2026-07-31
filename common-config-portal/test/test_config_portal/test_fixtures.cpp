#include "test_fixtures.h"

namespace test_fixtures {

const char* const kModes[] = {"daily", "hourly", "manual", nullptr};

const config_portal::Field kFields[] = {
    {"enabled", "Enabled", nullptr, config_portal::FieldType::Bool, "true", nullptr, 0, 0, nullptr},
    {"count", "Count", nullptr, config_portal::FieldType::Int, "5", nullptr, 1, 10, nullptr},
    {"ratio", "Ratio", nullptr, config_portal::FieldType::Float, "1.5", nullptr, 0, 0, nullptr},
    {"name", "Name", nullptr, config_portal::FieldType::String, "xkcd", nullptr, 0, 0, "kc"},
    {"mode", "Mode", nullptr, config_portal::FieldType::Enum, "daily", kModes, 0, 0, nullptr},
    {"api_key", "API key", nullptr, config_portal::FieldType::Secret, "", nullptr, 0, 0, nullptr},
};

const config_portal::Section kSections[] = {{"General", kFields, 6}};
const config_portal::Schema kSchema = {"xkcd", kSections, 1};

}  // namespace test_fixtures
