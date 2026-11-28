#ifndef CPLUS_SAFE_LOG_H
#define CPLUS_SAFE_LOG_H
#include <stddef.h>
#if defined(__cplusplus)
extern "C" {
#endif
typedef enum cplus_safe_log_level {
    CPLUS_SAFE_LOG_OFF = 0,
    CPLUS_SAFE_LOG_ERROR = 1,
    CPLUS_SAFE_LOG_WARN = 2,
    CPLUS_SAFE_LOG_INFO = 3,
    CPLUS_SAFE_LOG_DEBUG = 4,
    CPLUS_SAFE_LOG_TRACE = 5
} cplus_safe_log_level;
void cplus_safe_log_set_level(cplus_safe_log_level level);
cplus_safe_log_level cplus_safe_log_get_level(void);
void cplus_safe_log(cplus_safe_log_level level, const char *api, const char *msg);

/*
 * Rich-diagnostics integration. The exported cplus_safe_log symbol above is
 * unchanged (legacy output by default), but in-tree callers use the macro so
 * the exact source file, line and function are captured for rich mode.
 * Define CPLUS_SAFE_DIAG_NO_MACROS before including this header to opt out.
 */
#ifndef CPLUS_SAFE_DIAG_NO_MACROS
/* Location-aware variant: applies the same level filter as cplus_safe_log,
 * then routes through the diagnostic engine with the caller's source location. */
void cplus_safe_log_at(cplus_safe_log_level level, const char *api,
                       const char *msg, const char *file, int line,
                       const char *func);
#define cplus_safe_log(level, api, msg)                                        \
  cplus_safe_log_at((level), (api), (msg), __FILE__, __LINE__, __func__)
#endif
#if defined(__cplusplus)
}
#endif
#endif
