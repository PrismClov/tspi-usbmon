/*
 * Small logging helper.
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include "usbmon.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static enum um_log_level g_level = UM_LOG_INFO;

void um_log_set_level(enum um_log_level level)
{
    g_level = level;
}

static const char *level_name(enum um_log_level level)
{
    switch (level) {
    case UM_LOG_ERROR:
        return "E";
    case UM_LOG_WARN:
        return "W";
    case UM_LOG_INFO:
        return "I";
    default:
        return "D";
    }
}

void um_log(enum um_log_level level, const char *fmt, ...)
{
    struct timespec ts;
    struct tm tm;
    va_list ap;

    if (level > g_level)
        return;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    fprintf(stderr, "%02d:%02d:%02d.%03ld %s ", tm.tm_hour, tm.tm_min, tm.tm_sec,
            ts.tv_nsec / 1000000, level_name(level));
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}
