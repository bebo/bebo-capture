#pragma once

#undef warn
#undef error
#undef info
#undef debug

#undef DEBUG

#include "g2log.h"
#include "g2logworker.h"

#ifndef VERSION
#define VERSION "0.0.0"
#endif


#ifdef __cplusplus
extern "C" {
#endif

// backwards compatible log functions
#undef warn
#undef error
#undef info
#undef debug

#define debug(format, ...)  LOGF(DEBUG, format,  ##__VA_ARGS__ )
#define info(format, ...)  LOGF(INFO, format,  ##__VA_ARGS__ )
#define warn(format, ...)  LOGF(WARNING, format,  ##__VA_ARGS__ )
#define error(format, ...)  LOGF(WARNING, format,  ##__VA_ARGS__ )

void setupLogging();

#ifdef __cplusplus
}  // extern "C"
#endif
