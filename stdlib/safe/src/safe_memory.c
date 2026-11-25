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

/* Safeclib constraint codes (400/401/403/404/406, see safe_memory.h) are a
 * DIFFERENT domain from platform errno.  The checked API keeps returning them
 * verbatim, but a refusal also publishes a standard errno value: callers that
 * follow the common C habit of testing errno instead of the return code must
 * not mistake a refused operation for a successful one.  Success paths never
 * touch errno, so valid-input semantics are unchanged. */
/* Red-team finding (tests/redteam/memstr/FINDINGS.md §1): safeclib's own
 * constraint handler does `memset(dest, 0, dmax)` on a REFUSED operation
 * (safe_mem_constraint.h:handle_mem_error; memcpy_s.c overlap branch).  When
 * the caller supplies dmax > the real destination capacity, the refusal path
 * itself becomes the out-of-bounds write.  Raw `cplus_sc_*` entry points and
 * untracked stack destinations cannot be repaired from this layer (BOS is
 * unknown inside a non-inlined callee); see HARDENING.md for the honest scope.
 *
 * Here we refuse BEFORE calling safeclib whenever a capacity is provable:
 * tracked malloc_s metadata (including interior pointers) and
 * __builtin_object_size (useful if the compiler can see through the call).
 * A violation past that proven bound is reported as ESNOSPC (406) with
 * errno=ERANGE; safeclib's zeroing path is never entered, so no byte is
 * written.  Valid inputs (dmax <= proven capacity) proceed unchanged. */
#define CPLUS_SAFE_ESNOSPC 406

static int cplus_safe_refuse_unproven_dmax(const char *api, const void *dest,
                                           rsize_t dmax) {
    size_t provable = (size_t)-1;
    size_t tracked;
    size_t bos;
    char msg[160];

    if (dest == NULL) return 0; /* safeclib reports ESNULLP; no write happens */
    tracked = cplus_safe_remaining_capacity(dest);
    bos = (size_t)CPLUS_SAFE_OBJ_SIZE(dest);
    if (tracked != (size_t)-1) provable = tracked;
    if (bos != (size_t)-1 && (provable == (size_t)-1 || bos < provable))
        provable = bos;
    if (provable == (size_t)-1 || dmax <= provable) return 0;

    errno = ERANGE;
    cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "dmax exceeds proven capacity");
    cplus_safe_stat_record(api, 0);
    snprintf(msg, sizeof(msg),
             "refused before safeclib: dmax %zu > proven destination capacity "
             "%zu (ESNOSPC 406); error-path zeroing skipped",
             (size_t)dmax, provable);
    cplus_runtime_violation(api, msg);
    return 1;
}

static void cplus_safe_publish_constraint_errno(errno_t r) {
    switch (r) {
    case 400: /* ESNULLP */
    case 401: /* ESZEROL */
    case 404: /* ESOVRLP */
    case 410: /* ESLEWRNG */
        errno = EINVAL;
        break;
    case 403: /* ESLEMAX */
    case 406: /* ESNOSPC */
        errno = ERANGE;
        break;
#ifdef EOVERFLOW
    case EOVERFLOW: /* safeclib's EOVERFLOW is the platform errno value */
        errno = EOVERFLOW;
        break;
#endif
    default: /* any other positive constraint code still means "refused" */
        errno = EINVAL;
        break;
    }
}

errno_t cplus_memcpy_s(void *dest, rsize_t dmax, const void *src, rsize_t smax) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memcpy_s", "enter");
    if (cplus_safe_reject_uaf(dest, "memcpy_s") || cplus_safe_reject_uaf(src, "memcpy_s"))
        return 400;
    if (cplus_safe_refuse_unproven_dmax("memcpy_s", dest, dmax))
        return CPLUS_SAFE_ESNOSPC;
    errno_t r = cplus_sc_memcpy_s(dest, dmax, src, smax);
    if (r != 0) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memcpy_s", "constraint/error");
        cplus_safe_publish_constraint_errno(r);
    }
    cplus_safe_stat_record("memcpy_s", r == 0);
    return r;
}
errno_t cplus_memmove_s(void *dest, rsize_t dmax, const void *src, rsize_t smax) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memmove_s", "enter");
    if (cplus_safe_reject_uaf(dest, "memmove_s") || cplus_safe_reject_uaf(src, "memmove_s"))
        return 400;
    if (cplus_safe_refuse_unproven_dmax("memmove_s", dest, dmax))
        return CPLUS_SAFE_ESNOSPC;
    errno_t r = cplus_sc_memmove_s(dest, dmax, src, smax);
    if (r != 0) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memmove_s", "constraint/error");
        cplus_safe_publish_constraint_errno(r);
    }
    cplus_safe_stat_record("memmove_s", r == 0);
    return r;
}
errno_t cplus_memset_s(void *dest, rsize_t dmax, int value, rsize_t n) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memset_s", "enter");
    if (cplus_safe_reject_uaf(dest, "memset_s"))
        return 400;
    if (cplus_safe_refuse_unproven_dmax("memset_s", dest, dmax))
        return CPLUS_SAFE_ESNOSPC;
    errno_t r = cplus_sc_memset_s(dest, dmax, value, n);
    if (r != 0) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memset_s", "constraint/error");
        cplus_safe_publish_constraint_errno(r);
    }
    cplus_safe_stat_record("memset_s", r == 0);
    return r;
}
errno_t cplus_memcmp_s(const void *dest, rsize_t dmax, const void *src, rsize_t smax, int *diff) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memcmp_s", "enter");
    if (cplus_safe_reject_uaf(dest, "memcmp_s") || cplus_safe_reject_uaf(src, "memcmp_s")) {
        if (diff) *diff = -1;
        return 400;
    }
    if (cplus_safe_refuse_unproven_dmax("memcmp_s", dest, dmax)) {
        if (diff) *diff = -1;
        return CPLUS_SAFE_ESNOSPC;
    }
    errno_t r = cplus_sc_memcmp_s(dest, dmax, src, smax, diff);
    if (r != 0) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memcmp_s", "constraint/error");
        cplus_safe_publish_constraint_errno(r);
    }
    cplus_safe_stat_record("memcmp_s", r == 0);
    return r;
}
errno_t cplus_memzero_s(void *dest, rsize_t dmax) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "memzero_s", "enter");
    if (cplus_safe_reject_uaf(dest, "memzero_s"))
        return 400;
    if (cplus_safe_refuse_unproven_dmax("memzero_s", dest, dmax))
        return CPLUS_SAFE_ESNOSPC;
    errno_t r = cplus_sc_memzero_s(dest, dmax);
    if (r != 0) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memzero_s", "constraint/error");
        cplus_safe_publish_constraint_errno(r);
    }
    cplus_safe_stat_record("memzero_s", r == 0);
    return r;
}

/* Unknown capacity is NOT verified capacity. Default mode keeps raw-C
 * compatibility (the operation proceeds with a warning); strict mode refuses.
 * The caller can select the mode explicitly.
 *
 * The flag is written/read with atomics: it is process-wide and callers may
 * toggle it from one thread while another is inside an auto wrapper.  A plain
 * int would be a C11 data race (undefined behaviour).  This changes no symbol,
 * signature or single-threaded semantics. */
static int g_unknown_capacity_strict = 0;

void cplus_safe_set_unknown_capacity_mode(int strict) {
    __atomic_store_n(&g_unknown_capacity_strict, strict ? 1 : 0, __ATOMIC_RELEASE);
}

int cplus_safe_unknown_capacity_strict(void) {
    return __atomic_load_n(&g_unknown_capacity_strict, __ATOMIC_ACQUIRE);
}

static int cplus_safe_capacity(const void *ptr, size_t known_dmax,
                               size_t *out) {
    size_t tracked = cplus_safe_remaining_capacity(ptr);
    size_t cap = (size_t)-1;
    if (tracked != (size_t)-1) cap = tracked;
    /* A sub-object's compile-time size can be tighter than the enclosing
     * allocation. Use the minimum of both known bounds, never the looser. */
    if (known_dmax != (size_t)-1 && (cap == (size_t)-1 || known_dmax < cap))
        cap = known_dmax;
    if (cap == (size_t)-1) return 0;
    *out = cap;
    return 1;
}

/* Returns 1 when the operation must be refused, 0 when dmax is usable. */
static int cplus_safe_check_capacity(const char *api, const void *ptr, size_t n,
                                     size_t known_dmax, size_t *dmax_out) {
    if (cplus_safe_capacity(ptr, known_dmax, dmax_out)) return 0;
    if (cplus_safe_unknown_capacity_strict()) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "unknown destination capacity");
        cplus_safe_stat_record(api, 0);
        errno = EINVAL;
        return 1;
    }
    cplus_safe_log(CPLUS_SAFE_LOG_WARN, api,
                   "unknown destination capacity - unchecked");
    *dmax_out = n; /* no safety claim is made in this case */
    return 0;
}

static void cplus_safe_report_overflow(const char *api, size_t requested, size_t dmax) {
    char msg[128];
    errno = ERANGE;
    cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "destination buffer overflow prevented");
    cplus_safe_stat_record(api, 0);
    /* ESNOSPC (406) is the safeclib constraint code for "not enough space";
     * report it explicitly so callers can map the diagnostic to the checked
     * API's error convention as well as to errno = ERANGE. */
    snprintf(msg, sizeof(msg),
             "buffer overflow refused (requested %zu > capacity %zu, "
             "ESNOSPC 406)", requested, dmax);
    cplus_runtime_violation(api, msg);
}

/* ADDITIVE source-side bound proof for the standard-signature auto wrappers.
 *
 * The public signature has no source-capacity parameter (the ABI cannot
 * change), so compile-time __builtin_object_size cannot reach a stack source
 * from inside this function.  Heap sources allocated through
 * malloc_s/calloc_s/realloc_s ARE tracked, however.  cplus_memcmp_s_auto
 * already checks tracked capacity for both operands via
 * cplus_safe_check_capacity(); memcpy_s_auto / memmove_s_auto previously
 * checked only the destination.  This helper closes that asymmetry without
 * changing any signature, macro, exported symbol or valid-input behaviour:
 * it refuses ONLY when a capacity is provably known AND n exceeds it.
 * Unknown capacity keeps the pre-existing policy untouched. */
static int cplus_safe_check_src_capacity(const char *api, const void *ptr,
                                         size_t n) {
    size_t cap = cplus_safe_remaining_capacity(ptr);
    char msg[128];
    if (cap == (size_t)-1 || n <= cap) return 0;
    errno = ERANGE;
    cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "source over-read prevented");
    cplus_safe_stat_record(api, 0);
    snprintf(msg, sizeof(msg),
             "source over-read refused (requested %zu > proven source capacity "
             "%zu, ESNOSPC 406)", n, cap);
    cplus_runtime_violation(api, msg);
    return 1;
}

void *cplus_memcpy_s_auto(void *dest, const void *src, size_t n, size_t known_dmax) {
    size_t dmax;
    errno_t r;
    if (cplus_safe_reject_uaf(dest, "memcpy_s") || cplus_safe_reject_uaf(src, "memcpy_s"))
        return NULL;
    /* NULL is refused even at n == 0, consistently in both safety layers. */
    if (!dest || !src) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memcpy_s", "null pointer");
        cplus_safe_stat_record("memcpy_s", 0);
        return NULL;
    }
    if (n == 0) {
        cplus_safe_stat_record("memcpy_s", 1);
        return dest;
    }
    if (cplus_safe_check_capacity("mem*_s_auto", dest, n, known_dmax, &dmax)) return NULL;
    if (n > dmax || n > RSIZE_MAX_MEM) {
        cplus_safe_report_overflow("memcpy_s", n, dmax);
        return NULL;
    }
    if (cplus_safe_check_src_capacity("memcpy_s", src, n)) return NULL;
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
    /* NULL is refused even at n == 0, consistently in both safety layers. */
    if (!dest || !src) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memmove_s", "null pointer");
        cplus_safe_stat_record("memmove_s", 0);
        return NULL;
    }
    if (n == 0) {
        cplus_safe_stat_record("memmove_s", 1);
        return dest;
    }
    if (cplus_safe_check_capacity("mem*_s_auto", dest, n, known_dmax, &dmax)) return NULL;
    if (n > dmax || n > RSIZE_MAX_MEM) {
        cplus_safe_report_overflow("memmove_s", n, dmax);
        return NULL;
    }
    if (cplus_safe_check_src_capacity("memmove_s", src, n)) return NULL;
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
    if (cplus_safe_check_capacity("mem*_s_auto", dest, n, known_dmax, &dmax)) return NULL;
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
    /* NULL operands are constraint violations even at n == 0 (RED-20 E1):
     * check them BEFORE the zero-length success path. */
    if (!s1 || !s2) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "memcmp_s", "null pointer");
        cplus_safe_stat_record("memcmp_s", 0);
        /* A NULL operand is a constraint violation, never an "equal" result.
         * Return -1 (the error sentinel) for the both-NULL case too; the
         * one-NULL ordering sentinels are kept for compatibility. */
        return (s1 == s2) ? -1 : (s1 ? 1 : -1);
    }
    if (n == 0) {
        cplus_safe_stat_record("memcmp_s", 1);
        return 0;
    }
    if (cplus_safe_check_capacity("memcmp_s", s1, n, known_s1, &d1)) return -1;
    if (cplus_safe_check_capacity("memcmp_s", s2, n, known_s2, &d2)) return -1;
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
