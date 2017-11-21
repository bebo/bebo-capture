#pragma once

#undef warn
#undef error
#undef info
#undef debug

#undef DEBUG

#include "g2log.h"
#include "g2logworker.h"

#ifdef __cplusplus
extern "C" {
#endif

// backwards compatible log functions
#undef warn
#undef error
#undef info
#undef debug

#ifdef NDEBUG
#define debug(format, ...)
#define debug_(format, ...)
#else
#define debug(format, ...)  LOGF(DEBUG, TEXT(format),  ##__VA_ARGS__ )
#define debug_(format, ...)  LOGF(DEBUG, format,  ##__VA_ARGS__ )
#endif 

#define info(format, ...)  LOGF(INFO, TEXT(format),  ##__VA_ARGS__ )
#define warn(format, ...)  LOGF(WARNING, TEXT(format),  ##__VA_ARGS__ )
#define error(format, ...)  LOGF(ERR, TEXT(format),  ##__VA_ARGS__ )
#define info_(format, ...)  LOGF(INFO, format,  ##__VA_ARGS__ )
#define warn_(format, ...)  LOGF(WARNING, format,  ##__VA_ARGS__ )
#define error_(format, ...)  LOGF(ERR, format,  ##__VA_ARGS__ )



void setupLogging();
void logRotate();


#ifdef __cplusplus
}  // extern "C"
#endif
