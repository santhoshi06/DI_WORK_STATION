#include "debug.h"
#include <stdio.h>
#include <stdarg.h>
#include <windows.h>

LOG_LEVEL log_level = LOG_NOTICE;

void di_log(LOG_LEVEL level, const char *fmt, ...)
{
    if (log_level == LOG_NONE)
        return;

    if (log_level != LOG_ALL && level != log_level)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    printf("[%02d:%02d:%02d] ",
           st.wHour,
           st.wMinute,
           st.wSecond);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
