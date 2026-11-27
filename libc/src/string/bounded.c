/* Capacity-checked ordinary C memory/string operations for the PLAIN libc
 * layer. This makes unchanged `memset`/`strcpy` calls safer without linking
 * the C+ SafeCRT library; the C+ layer (stdlib/safe/safe_c.h) adds the same
 * guarantees with safeclib constraint handling.
 *
 * Honest scope: capacity is only known for (a) live blocks from cplus_malloc /
 * cplus_calloc / cplus_realloc (including INTERIOR pointers) and (b) stack or
 * global objects whose size Clang/GCC can compute with __builtin_object_size.
 * Unknown capacity is NEVER presented as verified capacity: default mode
 * preserves raw-C behaviour with a warning, strict mode refuses the call.
 * Pointer lifetime, provenance and format-string validity remain caller
 * responsibilities. No host libc symbol is interposed. */
#include "../../log/log.h"
#include "../../install-include/cplus_libc.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>

/* Mirror of the SafeCRT RSIZE_MAX_MEM bound (safe_memory.h). Rejecting lengths
 * above it is what keeps an unbounded n from ever being delegated to the raw
 * cplus_strncpy zero-fill loop. */
#ifndef RSIZE_MAX_MEM
#define RSIZE_MAX_MEM ((size_t)(SIZE_MAX >> 1))
#endif

/* Best-effort compile-time source-size proof. The public bounded signatures
 * carry a `known` argument for the DESTINATION only, so inside this callee a
 * stack/global source is normally BOS_UNKNOWN; tracked allocations are still
 * resolved by cplus_alloc_remaining_capacity() below (the libc counterpart of
 * cplus_safe_remaining_capacity()). */
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_BOUNDED_OBJ_SIZE(p) __builtin_object_size((p), 1)
#else
#define CPLUS_BOUNDED_OBJ_SIZE(p) ((size_t)-1)
#endif

/* Process-wide and intentionally race-free: one thread may toggle the mode
 * while another is inside a bounded operation.  Mirrors the atomic discipline
 * of stdlib/safe/src/safe_memory.c without changing the public API. */
static int g_unknown_strict = 0;

void cplus_libc_set_unknown_capacity_mode(int strict) {
    __atomic_store_n(&g_unknown_strict, strict ? 1 : 0, __ATOMIC_RELEASE);
}

static int unknown_capacity_is_strict(void) {
    return __atomic_load_n(&g_unknown_strict, __ATOMIC_ACQUIRE) != 0;
}

static size_t resolve_capacity(const void *p, size_t known) {
    size_t tracked = cplus_alloc_remaining_capacity(p);
    size_t cap = (size_t)-1;
    if (tracked != (size_t)-1) cap = tracked;
    if (known != (size_t)-1 && (cap == (size_t)-1 || known < cap)) cap = known;
    return cap;
}

/* Returns 1 when the call must be refused, 0 otherwise. */
static int check_capacity(const char *api, const void *p, size_t need,
                          size_t known, size_t *cap_out) {
    size_t cap = resolve_capacity(p, known);
    if (cap == (size_t)-1) {
        if (unknown_capacity_is_strict()) {
            cplus_libc_log(api, "unknown capacity - refused");
            errno = EINVAL;
            return 1;
        }
        cplus_libc_log(api, "unknown capacity - unchecked");
        return 0;
    }
    *cap_out = cap;
    if (need > cap) {
        cplus_libc_log(api, "overflow refused");
        errno = ERANGE;
        return 1;
    }
    return 0;
}

/* Source-side counterpart of check_capacity(). The public signature has no
 * `known_s` parameter, so an UNKNOWN source capacity cannot be a refusal
 * condition without breaking ordinary C calls with literals/stack sources.
 * Refuse ONLY when a capacity is provable (tracked allocation or BOS) and n
 * exceeds it: that is a heap/source over-read, which must never reach the raw
 * cplus_memcpy/cplus_memmove loop. Both checks run before any write, so the
 * destination is left intact on refusal. */
static int check_source_capacity(const char *api, const void *s, size_t n) {
    size_t cap = resolve_capacity(s, (size_t)CPLUS_BOUNDED_OBJ_SIZE(s));
    if (cap == (size_t)-1 || n <= cap) return 0;
    cplus_libc_log(api, "source over-read refused");
    errno = ERANGE;
    return 1;
}

/* Scan at most limit bytes for a terminator. Returns the number of bytes
 * before the first NUL, or limit when no NUL exists inside the bound. Never
 * touches byte[limit] or beyond. */
static size_t bounded_strlen(const char *s, size_t limit) {
    size_t k = 0;
    while (k < limit && s[k] != '\0') ++k;
    return k;
}

/* Best-effort source capacity. BOS_UNKNOWN/zero means the source has no
 * provable size in this callee; tracked heap sources still resolve exactly. */
static size_t source_capacity(const char *s) {
    size_t cap = resolve_capacity(s, (size_t)CPLUS_BOUNDED_OBJ_SIZE(s));
    if (cap == 0u) return (size_t)-1; /* BOS 0 == no object known, not empty */
    return cap;
}

/* Smallest of the available bounds; (size_t)-1 means unbounded/unknown. */
static size_t min_bound(size_t a, size_t b) {
    if (a == (size_t)-1) return b;
    if (b == (size_t)-1) return a;
    return a < b ? a : b;
}

/* Resolve the destination before any scan of d or s. A strict-mode refusal
 * for unknown capacity happens here; the final check_capacity() below still
 * enforces the resolved byte requirement. */
static int dest_capacity_or_refuse(const char *api, const void *d, size_t known,
                                   size_t *cap_out) {
    size_t cap = resolve_capacity(d, known);
    if (cap == (size_t)-1) {
        if (unknown_capacity_is_strict()) {
            cplus_libc_log(api, "unknown capacity - refused");
            errno = EINVAL;
            return 1;
        }
        cplus_libc_log(api, "unknown capacity - unchecked");
        return 0;
    }
    *cap_out = cap;
    return 0;
}

/* Guard (#2, AUDIT_memory_string.md): unknown capacity in default mode leaves
 * n unchecked above check_capacity(), so still reject absurd lengths here -
 * SIZE_MAX must never reach the raw copy/fill/compare loops. Known-capacity
 * overflow was already refused (ERANGE) by check_capacity, so error precedence
 * is unchanged. Same invariant as cplus_strncpy_bounded. */
static int cplus_refuse_absurd_length(size_t n) {
    if (n > RSIZE_MAX_MEM) { errno = EOVERFLOW; return 1; }
    return 0;
}

void *cplus_memset_bounded(void *d, int v, size_t n, size_t known) {
    size_t cap = 0;
    if (!d) { errno = EINVAL; return NULL; }
    if (check_capacity("memset", d, n, known, &cap)) return NULL;
    if (cplus_refuse_absurd_length(n)) return NULL;
    return cplus_memset(d, v, n);
}

void *cplus_memcpy_bounded(void *d, const void *s, size_t n, size_t known) {
    size_t cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    if (check_capacity("memcpy", d, n, known, &cap)) return NULL;
    if (check_source_capacity("memcpy(source)", s, n)) return NULL;
    if (cplus_refuse_absurd_length(n)) return NULL;
    return cplus_memcpy(d, s, n);
}

void *cplus_memmove_bounded(void *d, const void *s, size_t n, size_t known) {
    size_t cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    if (check_capacity("memmove", d, n, known, &cap)) return NULL;
    if (check_source_capacity("memmove(source)", s, n)) return NULL;
    if (cplus_refuse_absurd_length(n)) return NULL;
    return cplus_memmove(d, s, n);
}

int cplus_memcmp_bounded(const void *a, const void *b, size_t n,
                         size_t known_a, size_t known_b) {
    size_t cap = 0;
    if (!a || !b) { errno = EINVAL; return 0; }
    if (check_capacity("memcmp(a)", a, n, known_a, &cap)) return 0;
    if (check_capacity("memcmp(b)", b, n, known_b, &cap)) return 0;
    if (cplus_refuse_absurd_length(n)) return 0;
    return cplus_memcmp(a, b, n);
}

char *cplus_strcpy_bounded(char *d, const char *s, size_t known) {
    size_t dcap = (size_t)-1, scap, scan, slen, need, cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    /* Capacity first: the old cplus_strlen(s) ran before any bound was known
     * and read past unterminated heap sources. */
    if (dest_capacity_or_refuse("strcpy", d, known, &dcap)) return NULL;
    scap = source_capacity(s);
    scan = min_bound(dcap, scap);
    if (scan != (size_t)-1) {
        slen = bounded_strlen(s, scan);
        if (slen == scan) { /* no terminator inside a proven bound */
            cplus_libc_log("strcpy", "source over-read refused");
            errno = ERANGE;
            return NULL;
        }
    } else {
        /* Neither side has a provable bound: preserve raw-C behaviour. */
        slen = cplus_strlen(s);
    }
    if (slen == SIZE_MAX) { errno = EOVERFLOW; return NULL; }
    need = slen + 1u;
    if (check_capacity("strcpy", d, need, known, &cap)) return NULL;
    return cplus_strcpy(d, s);
}

char *cplus_strncpy_bounded(char *d, const char *s, size_t n, size_t known) {
    size_t cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    if (check_capacity("strncpy", d, n, known, &cap)) return NULL; /* ERANGE when known cap is too small */
    /* Guard (#2, AUDIT_memory_string.md): unknown capacity in default mode
     * leaves n unchecked above, so still reject absurd lengths before
     * delegation - SIZE_MAX must never reach the raw zero-fill loop. Known
     * capacity overflow was already refused (ERANGE) by check_capacity. */
    if (n > RSIZE_MAX_MEM) { errno = EOVERFLOW; return NULL; }
    /* Guard (#2): cap the effective length to the resolved destination
     * capacity. check_capacity already refuses n>cap when capacity is known,
     * so this is defence in depth that only constrains the delegated call. */
    if (cap != 0 && cap != (size_t)-1 && n > cap) n = cap;
    return cplus_strncpy(d, s, n);
}

char *cplus_strcat_bounded(char *d, const char *s, size_t known) {
    size_t dcap = (size_t)-1, scap, scan, dlen, slen, need, cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    /* Destination capacity is resolved BEFORE cplus_strlen(d): an uninitialised
     * cplus_malloc buffer must be bounded by its allocation, not scanned past. */
    if (dest_capacity_or_refuse("strcat", d, known, &dcap)) return NULL;
    if (dcap == 0u) { errno = ERANGE; return NULL; }
    if (dcap != (size_t)-1) {
        dlen = bounded_strlen(d, dcap);
        if (dlen == dcap) { /* d has no terminator in its own block */
            cplus_libc_log("strcat", "destination over-read refused");
            errno = ERANGE;
            return NULL;
        }
        scan = dcap - dlen - 1u; /* room for s's terminator */
    } else {
        dlen = cplus_strlen(d); /* no provable bound: legacy behaviour */
        scan = (size_t)-1;
    }
    scap = source_capacity(s);
    scan = min_bound(scan, scap);
    if (scan == 0u) {
        /* Only the source's first byte matters: an empty source is a valid
         * no-op, while any character would overflow the destination.  A C
         * string pointer is readable at least up to its terminator. */
        slen = (s[0] == '\0') ? 0u : 1u;
    } else if (scan != (size_t)-1) {
        slen = bounded_strlen(s, scan);
        if (slen == scan) {
            cplus_libc_log("strcat", "source over-read refused");
            errno = ERANGE;
            return NULL;
        }
    } else {
        slen = cplus_strlen(s);
    }
    /* With a proven destination bound the addition cannot overflow; without
     * one, guard both the exact wrap and the dlen == SIZE_MAX case. */
    if (slen >= SIZE_MAX - dlen) { errno = EOVERFLOW; return NULL; }
    need = dlen + slen + 1u;
    if (check_capacity("strcat", d, need, known, &cap)) return NULL;
    return cplus_strcat(d, s);
}

/* strncat may receive a source that is not NUL-terminated within n bytes:
 * scan at most n bytes, never the whole string like cplus_strlen(s). */
char *cplus_strncat_bounded(char *d, const char *s, size_t n, size_t known) {
    size_t dcap = (size_t)-1, scap, scan, dlen, slen, need, cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    if (dest_capacity_or_refuse("strncat", d, known, &dcap)) return NULL;
    if (dcap == 0u) { errno = ERANGE; return NULL; }
    if (dcap != (size_t)-1) {
        dlen = bounded_strlen(d, dcap);
        if (dlen == dcap) {
            cplus_libc_log("strncat", "destination over-read refused");
            errno = ERANGE;
            return NULL;
        }
        scan = dcap - dlen - 1u;
    } else {
        dlen = cplus_strlen(d);
        scan = (size_t)-1;
    }
    scan = min_bound(scan, n);
    scap = source_capacity(s);
    scan = min_bound(scan, scap);
    /* With no destination room, only the presence of a first source byte
     * decides whether this append would overflow. */
    slen = (scan == 0u) ? ((s[0] == '\0') ? 0u : 1u)
                        : bounded_strlen(s, scan); /* SIZE_MAX = legacy case */
    /* need = dlen + slen + 1 must not wrap SIZE_MAX into a small value that
     * would bypass check_capacity(). slen >= SIZE_MAX - dlen covers both the
     * exact wrap (dlen + slen == SIZE_MAX) and dlen == SIZE_MAX. */
    if (slen >= SIZE_MAX - dlen) { errno = EOVERFLOW; return NULL; }
    need = dlen + slen + 1u;
    if (check_capacity("strncat", d, need, known, &cap)) return NULL;
    return cplus_strncat(d, s, n);
}
