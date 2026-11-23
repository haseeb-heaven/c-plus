#define _POSIX_C_SOURCE 200112L
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS
#include "../safe_string.h"
#include "../safe_memory.h" /* unknown-capacity policy */
#include "../safe_alloc.h"
#include "../safe_log.h"
#include "../safe_runtime.h"
#include "../safe_stats.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

extern errno_t cplus_sc_strcpy_s(char *dest, rsize_t dmax, const char *src);
extern errno_t cplus_sc_strncpy_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);
extern errno_t cplus_sc_strcat_s(char *dest, rsize_t dmax, const char *src);
extern errno_t cplus_sc_strncat_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);
extern errno_t cplus_sc_strcmp_s(const char *dest, rsize_t dmax, const char *src, int *indicator);
extern rsize_t cplus_sc_strnlen_s(const char *str, rsize_t smax);
extern char *cplus_sc_strtok_s(char *restrict dest, rsize_t *restrict dmaxp, const char *restrict delim, char **restrict ptr);
extern errno_t cplus_sc_strerror_s(char *dest, rsize_t dmax, int errnum);

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

errno_t cplus_strcpy_s(char *dest, rsize_t dmax, const char *src) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "strcpy_s", "enter");
    if (cplus_safe_reject_uaf(dest, "strcpy_s") || cplus_safe_reject_uaf(src, "strcpy_s"))
        return 400;
    errno_t r = cplus_sc_strcpy_s(dest, dmax, src);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strcpy_s", "constraint/error");
    cplus_safe_stat_record("strcpy_s", r == 0);
    return r;
}
errno_t cplus_strncpy_s(char *dest, rsize_t dmax, const char *src, rsize_t slen) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "strncpy_s", "enter");
    if (cplus_safe_reject_uaf(dest, "strncpy_s") || cplus_safe_reject_uaf(src, "strncpy_s"))
        return 400;
    errno_t r = cplus_sc_strncpy_s(dest, dmax, src, slen);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strncpy_s", "constraint/error");
    cplus_safe_stat_record("strncpy_s", r == 0);
    return r;
}
errno_t cplus_strcat_s(char *dest, rsize_t dmax, const char *src) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "strcat_s", "enter");
    if (cplus_safe_reject_uaf(dest, "strcat_s") || cplus_safe_reject_uaf(src, "strcat_s"))
        return 400;
    errno_t r = cplus_sc_strcat_s(dest, dmax, src);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strcat_s", "constraint/error");
    cplus_safe_stat_record("strcat_s", r == 0);
    return r;
}
errno_t cplus_strncat_s(char *dest, rsize_t dmax, const char *src, rsize_t slen) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "strncat_s", "enter");
    if (cplus_safe_reject_uaf(dest, "strncat_s") || cplus_safe_reject_uaf(src, "strncat_s"))
        return 400;
    errno_t r = cplus_sc_strncat_s(dest, dmax, src, slen);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strncat_s", "constraint/error");
    cplus_safe_stat_record("strncat_s", r == 0);
    return r;
}
errno_t cplus_strcmp_s(const char *dest, rsize_t dmax, const char *src, int *indicator) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "strcmp_s", "enter");
    if (cplus_safe_reject_uaf(dest, "strcmp_s") || cplus_safe_reject_uaf(src, "strcmp_s")) {
        if (indicator) *indicator = 0;
        return 400;
    }
    errno_t r = cplus_sc_strcmp_s(dest, dmax, src, indicator);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strcmp_s", "constraint/error");
    cplus_safe_stat_record("strcmp_s", r == 0);
    return r;
}
rsize_t cplus_strnlen_s(const char *str, rsize_t smax) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "strnlen_s", "enter");
    if (cplus_safe_reject_uaf(str, "strnlen_s"))
        return 0;
    rsize_t r = cplus_sc_strnlen_s(str, smax);
    cplus_safe_stat_record("strnlen_s", 1);
    return r;
}


char *cplus_strtok_s(char *dest, rsize_t *dmax, const char *delim, char **ptr) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "strtok_s", "enter");
    if (cplus_safe_reject_uaf(dest, "strtok_s"))
        return NULL;
    char *r = cplus_sc_strtok_s(dest, dmax, delim, ptr);
    if (!r) cplus_safe_log(CPLUS_SAFE_LOG_WARN, "strtok_s", "null");
    cplus_safe_stat_record("strtok_s", r != 0);
    return r;
}


errno_t cplus_strerror_s(char *dest, rsize_t dmax, int errnum) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "strerror_s", "enter");
    if (cplus_safe_reject_uaf(dest, "strerror_s"))
        return 400;
    errno_t r = cplus_sc_strerror_s(dest, dmax, errnum);
    if (r != 0) cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strerror_s", "constraint/error");
    cplus_safe_stat_record("strerror_s", r == 0);
    return r;
}

/* Resolve destination capacity using the REMAINING bytes of a tracked live
 * block (interior pointers included), then __builtin_object_size, then the
 * caller's fallback. Unknown capacity is refused in strict mode. */
static int cplus_safe_resolve_str_dmax(const char *dest, size_t fallback,
                                       size_t known_dmax, size_t *out) {
    size_t tracked = cplus_safe_remaining_capacity(dest);
    size_t cap = (size_t)-1;
    if (tracked != (size_t)-1) cap = tracked;
    if (known_dmax != (size_t)-1 && (cap == (size_t)-1 || known_dmax < cap))
        cap = known_dmax;
    if (cap != (size_t)-1) { *out = cap; return 1; }
    if (cplus_safe_unknown_capacity_strict()) return 0;
    *out = fallback; /* no safety claim is made for unknown capacity */
    return 1;
}

/* Defense-in-depth for the standard-C-signature (auto) wrappers.
 *
 * The checked safeclib primitives already bound every read they perform by
 * dmax/slen.  The wrappers, however, used to call strlen() on caller buffers
 * to compute a fallback dmax - an unterminated src/dest therefore caused an
 * unbounded over-read before safeclib could refuse it.  We now:
 *
 *   1. derive a hard scan ceiling from the destination's provable capacity
 *      (tracked remaining bytes and/or __builtin_object_size), capped at the
 *      RSIZE_MAX_STR ceiling safeclib itself enforces; and
 *   2. use the bounded cplus_strnlen_s() query instead of strlen(), refusing
 *      when no NUL appears within that bound.
 *
 * A string that is not terminated within the destination capacity could never
 * be copied/appended successfully, so this only converts a malformed input
 * from an unbounded read into a defined refusal (NULL + ERANGE).  All
 * valid-input results and the public ABI are unchanged; these are file-local
 * static helpers and no signature, symbol or layout is altered. */
static size_t cplus_safe_str_scan_ceiling(const char *dest, size_t known_dmax) {
    size_t cap = (size_t)-1;
    size_t tracked;
    if (!dest) return RSIZE_MAX_STR;
    tracked = cplus_safe_remaining_capacity(dest);
    if (tracked != (size_t)-1) cap = tracked;
    if (known_dmax != (size_t)-1 && (cap == (size_t)-1 || known_dmax < cap))
        cap = known_dmax;
    if (cap == (size_t)-1 || cap > RSIZE_MAX_STR) cap = RSIZE_MAX_STR;
    return cap;
}

static int cplus_safe_bounded_strlen(const char *s, size_t ceiling, size_t *out) {
    size_t n;
    if (!s || !out || ceiling == 0) return 0;
    if (ceiling > RSIZE_MAX_STR) ceiling = RSIZE_MAX_STR;
    n = cplus_strnlen_s(s, ceiling); /* bounded read, never strlen() */
    if (n >= ceiling) return 0;      /* no NUL within the bound */
    *out = n;
    return 1;
}

static void cplus_safe_str_refuse_unterminated(const char *api) {
    cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "unterminated string within bound");
    cplus_safe_stat_record(api, 0);
    errno = ERANGE;
}

char *cplus_strcpy_s_auto(char *dest, const char *src, size_t known_dmax) {
    size_t len, dmax;
    errno_t r;
    if (cplus_safe_reject_uaf(dest, "strcpy_s") || cplus_safe_reject_uaf(src, "strcpy_s"))
        return NULL;
    if (!dest || !src) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strcpy_s", "null pointer");
        cplus_safe_stat_record("strcpy_s", 0);
        return NULL;
    }
    len = 0;
    if (!cplus_safe_bounded_strlen(src, cplus_safe_str_scan_ceiling(dest, known_dmax), &len)) {
        cplus_safe_str_refuse_unterminated("strcpy_s");
        return NULL;
    }
    if (!cplus_safe_resolve_str_dmax(dest, (len < RSIZE_MAX_STR) ? (len + 1u) : RSIZE_MAX_STR, known_dmax, &dmax)) {

        errno = EINVAL;

        return NULL;

    }
    r = cplus_strcpy_s(dest, dmax, src);
    if (r != 0) {
        errno = (r == 406 || len + 1u > dmax) ? ERANGE : EINVAL;
        return NULL;
    }
    return dest;
}

char *cplus_strncpy_s_auto(char *dest, const char *src, size_t n, size_t known_dmax) {
    size_t dmax;
    errno_t r;
    if (cplus_safe_reject_uaf(dest, "strncpy_s") || cplus_safe_reject_uaf(src, "strncpy_s"))
        return NULL;
    if (!dest || !src) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strncpy_s", "null pointer");
        cplus_safe_stat_record("strncpy_s", 0);
        return NULL;
    }
    if (n == 0) {
        cplus_safe_stat_record("strncpy_s", 1);
        return dest;
    }
    /* No unbounded read here: safeclib strncpy_s reads at most slen (n) bytes
     * from src and the fallback dmax is n+1, never strlen(src). */
    if (!cplus_safe_resolve_str_dmax(dest, (n < RSIZE_MAX_STR) ? (n + 1u) : RSIZE_MAX_STR, known_dmax, &dmax)) {

        errno = EINVAL;

        return NULL;

    }
    r = cplus_strncpy_s(dest, dmax, src, n);
    if (r != 0) {
        errno = (r == 406 || n >= dmax) ? ERANGE : EINVAL;
        return NULL;
    }
    return dest;
}

char *cplus_strcat_s_auto(char *dest, const char *src, size_t known_dmax) {
    size_t dlen, slen, dmax;
    errno_t r;
    if (cplus_safe_reject_uaf(dest, "strcat_s") || cplus_safe_reject_uaf(src, "strcat_s"))
        return NULL;
    if (!dest || !src) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strcat_s", "null pointer");
        cplus_safe_stat_record("strcat_s", 0);
        return NULL;
    }
    /* Bound both scans by the destination's provable capacity: a string that
     * is not terminated within dmax could never be concatenated anyway. */
    {
        size_t ceiling = cplus_safe_str_scan_ceiling(dest, known_dmax);
        dlen = 0;
        slen = 0;
        if (!cplus_safe_bounded_strlen(dest, ceiling, &dlen) ||
            !cplus_safe_bounded_strlen(src, ceiling, &slen)) {
            cplus_safe_str_refuse_unterminated("strcat_s");
            return NULL;
        }
    }
    if (dlen > RSIZE_MAX_STR - slen - 1u) {
        errno = ERANGE;
        cplus_safe_stat_record("strcat_s", 0);
        return NULL;
    }
    if (!cplus_safe_resolve_str_dmax(dest, dlen + slen + 1u, known_dmax, &dmax)) {

        errno = EINVAL;

        return NULL;

    }
    r = cplus_strcat_s(dest, dmax, src);
    if (r != 0) {
        errno = (r == 406 || dlen + slen + 1u > dmax) ? ERANGE : EINVAL;
        return NULL;
    }
    return dest;
}

char *cplus_strncat_s_auto(char *dest, const char *src, size_t n, size_t known_dmax) {
    size_t dlen, dmax;
    errno_t r;
    if (cplus_safe_reject_uaf(dest, "strncat_s") || cplus_safe_reject_uaf(src, "strncat_s"))
        return NULL;
    if (!dest || !src) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "strncat_s", "null pointer");
        cplus_safe_stat_record("strncat_s", 0);
        return NULL;
    }
    if (n == 0) {
        cplus_safe_stat_record("strncat_s", 1);
        return dest;
    }
    /* Only dest needs an unbounded-scan fix: safeclib strncat_s reads at most
     * slen (n) bytes from src.  Bound the dest scan by its provable capacity. */
    dlen = 0;
    if (!cplus_safe_bounded_strlen(dest, cplus_safe_str_scan_ceiling(dest, known_dmax), &dlen)) {
        cplus_safe_str_refuse_unterminated("strncat_s");
        return NULL;
    }
    if (n >= RSIZE_MAX_STR - dlen) {
        errno = ERANGE;
        cplus_safe_stat_record("strncat_s", 0);
        return NULL;
    }
    if (!cplus_safe_resolve_str_dmax(dest, dlen + n + 1u, known_dmax, &dmax)) {

        errno = EINVAL;

        return NULL;

    }
    r = cplus_strncat_s(dest, dmax, src, n);
    if (r != 0) {
        errno = (r == 406 || dlen + n + 1u > dmax) ? ERANGE : EINVAL;
        return NULL;
    }
    return dest;
}
