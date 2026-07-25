#pragma once

#include "timestamped_logger.h"

// The application logger is defined once in main.cpp and used from any
// translation unit that includes this header.
extern TimestampedLogger appLog;

// Provider / helper translation units use the same short LOG spelling that
// main.cpp uses. Undefined here first so multiple includes stay clean.
#ifdef LOG
#undef LOG
#endif
#define LOG appLog
