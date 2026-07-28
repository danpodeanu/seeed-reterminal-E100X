#pragma once

#include "app_logger.h"

// Wires the shared appLog to a rotating file in /logs/ on the SD card.
//
// Layout: /logs/current.log is written to for the current boot;
// /logs/previous.log holds the log from the previous boot. On install
// the existing previous.log is unlinked and current.log is renamed
// into it, so there are always at most two files under /logs/ and the
// most recent completed boot is always available for triage.
//
// Enabling this incurs meaningful per-line SD I/O (flush + fsync per
// log line), so gate the call from each app behind a config flag --
// config::LOG_TO_SD by convention -- and only turn it on while
// troubleshooting.
namespace log_sd_sink {

// Path constants exposed so tests / tools can address the same files.
constexpr const char* kLogsDir = "/logs";
constexpr const char* kCurrentLogPath = "/logs/current.log";
constexpr const char* kPreviousLogPath = "/logs/previous.log";

// Rotate the previous boot's log and attach a fresh file handle to
// `logger`. Silent no-op when the SD card is not mounted or the file
// cannot be opened; the caller does not need to check the return.
// Must be called after sd_card::mount() has succeeded.
void install(TimestampedLogger& logger);

}  // namespace log_sd_sink
