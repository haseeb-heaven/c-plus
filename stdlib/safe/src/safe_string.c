#include "../safe_string.h"
#include "../safe_log.h"

extern errno_t cplus_upstream_strcpy_s(char *dest, rsize_t dmax, const char *src);
extern errno_t cplus_upstream_strncpy_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);
extern errno_t cplus_upstream_strcat_s(char *dest, rsize_t dmax, const char *src);
extern errno_t cplus_upstream_strncat_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);
extern rsize_t cplus_upstream_strnlen_s(const char *str, rsize_t smax);

errno_t cplus_strcpy_s(char *dest, rsize_t dmax, const char *src) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "strcpy_s", "enter");
    errno_t r = cplus_upstream_strcpy_s(dest, dmax, src);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strcpy_s", "constraint/error");
    return r;
}
errno_t cplus_strncpy_s(char *dest, rsize_t dmax, const char *src, rsize_t slen) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "strncpy_s", "enter");
    errno_t r = cplus_upstream_strncpy_s(dest, dmax, src, slen);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strncpy_s", "constraint/error");
    return r;
}
errno_t cplus_strcat_s(char *dest, rsize_t dmax, const char *src) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "strcat_s", "enter");
    errno_t r = cplus_upstream_strcat_s(dest, dmax, src);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strcat_s", "constraint/error");
    return r;
}
errno_t cplus_strncat_s(char *dest, rsize_t dmax, const char *src, rsize_t slen) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "strncat_s", "enter");
    errno_t r = cplus_upstream_strncat_s(dest, dmax, src, slen);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strncat_s", "constraint/error");
    return r;
}
rsize_t cplus_strnlen_s(const char *str, rsize_t smax) {
    return cplus_upstream_strnlen_s(str, smax);
}
