#include "log_sd_sink.h"

#include <SD.h>

#include "app_logger.h"
#include "sd_card.h"

namespace log_sd_sink {

void install(TimestampedLogger& logger) {
  // Ensure /logs/ exists. mkdir on an existing directory returns false
  // on the ESP-IDF SD stack, so we don't check the return value -- a
  // subsequent openForAppend() will succeed either way if the
  // directory is present.
  sd_card::makeDir(kLogsDir);

  // Rotate: unlink the previous slot (SD.rename refuses to overwrite),
  // then move the current log into it. Both steps are best-effort;
  // failure here just means the next boot starts a fresh current.log
  // alongside a stale previous.log.
  if (sd_card::fileExists(kCurrentLogPath)) {
    sd_card::removeFile(kPreviousLogPath);
    sd_card::renameFile(kCurrentLogPath, kPreviousLogPath);
  }

  // openForAppend creates the file if missing. Rotation just ran, so
  // this is normally a fresh empty file; if rotation failed we simply
  // continue appending to the old current.log rather than losing lines.
  File sink = sd_card::openForAppend(kCurrentLogPath);
  if (!sink) {
    LOG.printf("[log-sink] failed to open %s; SD logging disabled\n",
               kCurrentLogPath);
    return;
  }
  logger.attachSdSink(std::move(sink));
  // First line into the new current.log (via the tee) doubles as a
  // marker so a triage reader can see where each boot's log starts.
  LOG.printf("[log-sink] tee enabled -> %s (previous boot: %s)\n",
             kCurrentLogPath, kPreviousLogPath);
}

}  // namespace log_sd_sink
