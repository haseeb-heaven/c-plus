#define _POSIX_C_SOURCE 200112L
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS
#include "../safe_memory.h"
#include "../safe_alloc.h"
#include "../safe_log.h"
#include "../safe_runtime.h"
#include "../safe_stats.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* Upstream safeclib symbols, renamed at build time via -D. */
extern errno_t cplus_sc_memcpy_s(void *dest, rsize_t dmax, const void *src, rsize_t smax);
extern errno_t cplus_sc_memmove_s(void *dest, rsize_t dmax, const void *src, rsize_t smax);
extern errno_t cplus_sc_memset_s(void *dest, rsize_t dmax, int value, rsize_t n);
extern errno_t cplus_sc_memcmp_s(const void *dest, rsize_t dmax, const void *src, rsize_t smax, int *diff);
extern errno_t cplus_sc_memzero_s(void *dest, rsize_t dmax);

static int cplus_safe_reject_uaf(const void *ptr, const char *api) {
    if (ptr != NULL && cplus_safe_ptr_was_freed(ptr)) {
        char msg[128];
        errno = EFAULT;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "use-after-free refused");
        cplus_safe_stat_record(api, 0);
        cplus_safe_stat_record("use_after_free", 0);
        snprintf(msg, sizeof(msg),
                 "use-after-free detected (pointer %p was already freed)",
                 (const void *)ptr);
        cplus_runtime_violation(api, msg);
        return 1;
    }
    return 0;
}

errno_t cplus_memcpy_s(void *dest, rsize_t dmax, const void *src, rsize_t smax) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memcpy_s", "enter");
    if (cplus_safe_reject_uaf(dest, "memcpy_s") || cplus_safe_reject_uaf(src, "memcpy_s"))
        return 400;
    errno_t r = cplus_sc_memcpy_s(dest, dmax, src, smax);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memcpy_s", "constraint/error");
    cplus_safe_stat_record("memcpy_s", r == 0);
    return r;
}
errno_t cplus_memmove_s(void *dest, rsize_t dmax, const void *src, rsize_t smax) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memmove_s", "enter");
    if (cplus_safe_reject_uaf(dest, "memmove_s") || cplus_safe_reject_uaf(src, "memmove_s"))
        return 400;
    errno_t r = cplus_sc_memmove_s(dest, dmax, src, smax);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memmove_s", "constraint/error");
    cplus_safe_stat_record("memmove_s", r == 0);
    return r;
}
errno_t cplus_memset_s(void *dest, rsize_t dmax, int value, rsize_t n) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memset_s", "enter");
    if (cplus_safe_reject_uaf(dest, "memset_s"))
        return 400;
    errno_t r = cplus_sc_memset_s(dest, dmax, value, n);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memset_s", "constraint/error");
    cplus_safe_stat_record("memset_s", r == 0);
    return r;
}
errno_t cplus_memcmp_s(const void *dest, rsize_t dmax, const void *src, rsize_t smax, int *diff) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memcmp_s", "enter");
    if (cplus_safe_reject_uaf(dest, "memcmp_s") || cplus_safe_reject_uaf(src, "memcmp_s")) {
        if (diff) *diff = -1;
        return 400;
    }
    errno_t r = cplus_sc_memcmp_s(dest, dmax, src, smax, diff);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memcmp_s", "constraint/error");
    cplus_safe_stat_record("memcmp_s", r == 0);
    return r;
}
errno_t cplus_memzero_s(void *dest, rsize_t dmax) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memzero_s", "enter");
    if (cplus_safe_reject_uaf(dest, "memzero_s"))
        return 400;
    errno_t r = cplus_sc_memzero_s(dest, dmax);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memzero_s", "constraint/error");
    cplus_safe_stat_record("memzero_s", r == 0);
    return r;
}

static size_t cplus_safe_resolve_dmax(const void *ptr, size_t n, size_t known_dmax) {
    size_t tracked = cplus_safe_ptr_size(ptr);
    if (tracked > 0)
        return tracked;
    if (known_dmax != (size_t)-1)
        return known_dmax;
    return n;
}

static void cplus_safe_report_overflow(const char *api, size_t requested, size_t dmax) {
    char msg[128];
    errno = ERANGE;
    cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "destination buffer overflow prevented");
    cplus_safe_stat_record(api, 0);
    snprintf(msg, sizeof(msg),
             "buffer overflow refused (requested %zu > capacity %zu)",
             requested, dmax);
    cplus_runtime_violation(api, msg);
}

void *cplus_memcpy_s_auto(void *dest, const void *src, size_t n, size_t known_dmax) {
    size_t dmax;
    errno_t r;
    if (cplus_safe_reject_uaf(dest, "memcpy_s") || cplus_safe_reject_uaf(src, "memcpy_s"))
        return NULL;
    if (n == 0) {
        if (!dest) {
            errno = EINVAL;
            cplus_safe_stat_record("memcpy_s", 0);
            return NULL;
        }
        cplus_safe_stat_record("memcpy_s", 1);
        return dest;
    }
    if (!dest || !src) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memcpy_s", "null pointer");
        cplus_safe_stat_record("memcpy_s", 0);
        return NULL;
    }
    dmax = cplus_safe_resolve_dmax(dest, n, known_dmax);
    if (n > dmax || n > RSIZE_MAX_MEM) {
        cplus_safe_report_overflow("memcpy_s", n, dmax);
        return NULL;
    }
    r = cplus_memcpy_s(dest, dmax, src, n);
    if (r != 0) {
        errno = EINVAL;
        return NULL;
    }
    return dest;
}

void *cplus_memmove_s_auto(void *dest, const void *src, size_t n, size_t known_dmax) {
    size_t dmax;
    errno_t r;
    if (cplus_safe_reject_uaf(dest, "memmove_s") || cplus_safe_reject_uaf(src, "memmove_s"))
        return NULL;
    if (n == 0) {
        if (!dest) {
            errno = EINVAL;
            cplus_safe_stat_record("memmove_s", 0);
            return NULL;
        }
        cplus_safe_stat_record("memmove_s", 1);
        return dest;
    }
    if (!dest || !src) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memmove_s", "null pointer");
        cplus_safe_stat_record("memmove_s", 0);
        return NULL;
    }
    dmax = cplus_safe_resolve_dmax(dest, n, known_dmax);
    if (n > dmax || n > RSIZE_MAX_MEM) {
        cplus_safe_report_overflow("memmove_s", n, dmax);
        return NULL;
    }
    r = cplus_memmove_s(dest, dmax, src, n);
    if (r != 0) {
        errno = EINVAL;
        return NULL;
    }
    return dest;
}

void *cplus_memset_s_auto(void *dest, int value, size_t n, size_t known_dmax) {
    size_t dmax;
    errno_t r;
    if (cplus_safe_reject_uaf(dest, "memset_s"))
        return NULL;
    if (!dest) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memset_s", "null pointer");
        cplus_safe_stat_record("memset_s", 0);
        return NULL;
    }
    if (n == 0) {
        cplus_safe_stat_record("memset_s", 1);
        return dest;
    }
    dmax = cplus_safe_resolve_dmax(dest, n, known_dmax);
    if (n > dmax || n > RSIZE_MAX_MEM) {
        cplus_safe_report_overflow("memset_s", n, dmax);
        return NULL;
    }
    r = cplus_memset_s(dest, dmax, value, n);
    if (r != 0) {
        errno = EINVAL;
        return NULL;
    }
    return dest;
}

int cplus_memcmp_s_auto(const void *s1, const void *s2, size_t n, size_t known_s1, size_t known_s2) {
    size_t d1, d2;
    int diff = 0;
    errno_t r;
    if (cplus_safe_reject_uaf(s1, "memcmp_s") || cplus_safe_reject_uaf(s2, "memcmp_s"))
        return -1;
    if (n == 0) {
        cplus_safe_stat_record("memcmp_s", 1);
        return 0;
    }
    if (!s1 || !s2) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memcmp_s", "null pointer");
        cplus_safe_stat_record("memcmp_s", 0);
        return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    }
    d1 = cplus_safe_resolve_dmax(s1, n, known_s1);
    d2 = cplus_safe_resolve_dmax(s2, n, known_s2);
    if (n > d1 || n > d2 || n > RSIZE_MAX_MEM) {
        cplus_safe_report_overflow("memcmp_s", n, (n > d1) ? d1 : d2);
        return -1;
    }
    r = cplus_memcmp_s(s1, d1, s2, n, &diff);
    if (r != 0) {
        errno = EINVAL;
        return -1;
    }
    return diff;
}
