#ifndef LOG_H
#define LOG_H

enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL };

#define log_debug(...) _log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...) _log(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...) _log(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) _log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_fatal(...) _log(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

void _log(int level, const char *file, int line, const char *fmt, ...);

#endif
