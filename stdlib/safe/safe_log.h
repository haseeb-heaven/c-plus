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
#if defined(__cplusplus)
}
#endif
#endif
