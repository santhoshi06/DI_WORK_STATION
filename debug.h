#ifndef DEBUG_H
#define DEBUG_H

#include <stdarg.h>

typedef enum
{
    LOG_NONE = 0,
    LOG_PRIORITY,
    LOG_NOTICE,
    LOG_ALL
} LOG_LEVEL;

extern LOG_LEVEL log_level;

void di_log(LOG_LEVEL level, const char *fmt, ...);

#define LOG_PRIORITY(...)  di_log(LOG_PRIORITY, __VA_ARGS__)
#define LOG_NOTICE(...)    di_log(LOG_NOTICE, __VA_ARGS__)

#endif#endif
