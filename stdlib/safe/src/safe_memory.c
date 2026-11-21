#include "../safe_memory.h"
#include "../safe_log.h"

/* Upstream Intel safestringlib symbols, renamed at build time. */
extern errno_t cplus_upstream_memcpy_s(void *dest, rsize_t dmax, const void *src, rsize_t smax);
extern errno_t cplus_upstream_memmove_s(void *dest, rsize_t dmax, const void *src, rsize_t smax);
extern errno_t cplus_upstream_memset_s(void *dest, rsize_t dmax, int value, rsize_t n);
extern errno_t cplus_upstream_memcmp_s(const void *dest, rsize_t dmax, const void *src, rsize_t smax, int *diff);
extern errno_t cplus_upstream_memzero_s(void *dest, rsize_t dmax);

errno_t cplus_memcpy_s(void *dest, rsize_t dmax, const void *src, rsize_t smax) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memcpy_s", "enter");
    errno_t r = cplus_upstream_memcpy_s(dest, dmax, src, smax);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memcpy_s", "constraint/error");
    return r;
}
errno_t cplus_memmove_s(void *dest, rsize_t dmax, const void *src, rsize_t smax) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memmove_s", "enter");
    errno_t r = cplus_upstream_memmove_s(dest, dmax, src, smax);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memmove_s", "constraint/error");
    return r;
}
errno_t cplus_memset_s(void *dest, rsize_t dmax, int value, rsize_t n) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memset_s", "enter");
    errno_t r = cplus_upstream_memset_s(dest, dmax, value, n);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memset_s", "constraint/error");
    return r;
}
errno_t cplus_memcmp_s(const void *dest, rsize_t dmax, const void *src, rsize_t smax, int *diff) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memcmp_s", "enter");
    errno_t r = cplus_upstream_memcmp_s(dest, dmax, src, smax, diff);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memcmp_s", "constraint/error");
    return r;
}
errno_t cplus_memzero_s(void *dest, rsize_t dmax) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memzero_s", "enter");
    errno_t r = cplus_upstream_memzero_s(dest, dmax);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memzero_s", "constraint/error");
    return r;
}
