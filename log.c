#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static const char *levels[] = {"debug", "info", "warn", "error", "fatal"};

static const int colors[] = {34, 34, 33, 31, 31};

void _log(int level, const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    fprintf(stdout, "\033[1m%s:%d:\033[0m \033[1;%dm%s:\033[0m ", file, line,
            colors[level], levels[level]);
    vfprintf(stdout, fmt, args);
    fputc('\n', stdout);

    va_end(args);

    if (level == LOG_FATAL)
        exit(1);
}
