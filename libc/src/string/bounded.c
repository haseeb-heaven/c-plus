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

/* Set/read concurrently by callers; use atomics to avoid a C11 data race
 * (mirrors safe_memory.c's g_unknown_capacity_strict). */
static int g_unknown_strict = 0;

void cplus_libc_set_unknown_capacity_mode(int strict) {
    __atomic_store_n(&g_unknown_strict, strict ? 1 : 0, __ATOMIC_RELEASE);
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
        if (__atomic_load_n(&g_unknown_strict, __ATOMIC_ACQUIRE)) {
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

/* Guard (#2, AUDIT_memory_string.md): unknown capacity in default mode leaves
 * n unchecked above check_capacity(), so still reject absurd lengths here -
 * SIZE_MAX must never reach the raw copy/fill/compare loops. Known-capacity
 * overflow was already refused (ERANGE) by check_capacity, so error precedence
 * is unchanged. Same invariant as cplus_strncpy_bounded. */
static int cplus_refuse_absurd_length(size_t n) {
    if (n > RSIZE_MAX_MEM) { errno = EOVERFLOW; return 1; }
    return 0;
}

/* Source-side twin of check_capacity(): the public signatures have no source
 * capacity parameter, but heap sources from cplus_malloc/calloc/realloc are
 * tracked.  Refuse only when a capacity is provably known AND n exceeds it;
 * unknown capacity keeps the pre-existing permissive policy.  Closing this
 * asymmetry prevents a heap over-read / memory disclosure in memcpy/memmove. */
static int check_src_capacity(const char *api, const void *s, size_t n) {
    size_t cap = cplus_alloc_remaining_capacity(s);
    if (cap == (size_t)-1 || n <= cap) return 0;
    cplus_libc_log(api, "source over-read refused");
    errno = ERANGE;
    return 1;
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
    if (check_src_capacity("memcpy(src)", s, n)) return NULL;
    if (cplus_refuse_absurd_length(n)) return NULL;
    return cplus_memcpy(d, s, n);
}

void *cplus_memmove_bounded(void *d, const void *s, size_t n, size_t known) {
    size_t cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    if (check_capacity("memmove", d, n, known, &cap)) return NULL;
    if (check_src_capacity("memmove(src)", s, n)) return NULL;
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

/* Resolve the destination capacity BEFORE scanning any string. A strict-mode
 * unknown capacity is therefore refused before an unbounded read can happen.
 * When capacity is unknown in default mode, scans still use a finite ceiling
 * instead of raw strlen(). */
static int resolve_scan_ceiling(const char *api, const void *d, size_t known,
                                size_t *out)
{
    size_t cap = (size_t)-1;
    if (check_capacity(api, d, 1u, known, &cap)) return 1;
    *out = (cap == (size_t)-1) ? RSIZE_MAX_MEM : cap;
    return 0;
}

/* A tracked heap source cannot be scanned past its own allocation either. */
static size_t source_scan_ceiling(const char *s, size_t ceiling)
{
    size_t tracked = cplus_alloc_remaining_capacity(s);
    if (tracked != (size_t)-1 && tracked < ceiling) return tracked;
    return ceiling;
}

/* Bounded strlen: refuses when no NUL exists within ceiling bytes. */
static int bounded_strlen(const char *s, size_t ceiling, size_t *out)
{
    size_t n = cplus_strnlen(s, ceiling);
    if (n >= ceiling) { errno = ERANGE; return 1; }
    *out = n;
    return 0;
}

char *cplus_strcpy_bounded(char *d, const char *s, size_t known) {
    size_t need, slen, ceiling, cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    if (resolve_scan_ceiling("strcpy", d, known, &ceiling)) return NULL;
    /* The NUL must fit in the resolved destination capacity. */
    if (bounded_strlen(s, source_scan_ceiling(s, ceiling), &slen)) return NULL;
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
    size_t need, dlen, slen, ceiling, cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    if (resolve_scan_ceiling("strcat", d, known, &ceiling)) return NULL;
    /* Scan d only inside its own resolved capacity: an unterminated heap
     * buffer used to be read straight past the block by cplus_strlen(d). */
    if (bounded_strlen(d, ceiling, &dlen)) return NULL;
    if (ceiling <= dlen) { errno = ERANGE; return NULL; }
    if (bounded_strlen(s, source_scan_ceiling(s, ceiling - dlen), &slen)) return NULL;
    if (slen >= SIZE_MAX - dlen) { errno = EOVERFLOW; return NULL; }
    need = dlen + slen + 1u;
    if (check_capacity("strcat", d, need, known, &cap)) return NULL;
    return cplus_strcat(d, s);
}

char *cplus_strncat_bounded(char *d, const char *s, size_t n, size_t known) {
    size_t need, ceiling, cap = 0;
    size_t dlen, slen, scan_n, room = (size_t)-1;
    if (!d || !s) { errno = EINVAL; return NULL; }
    if (resolve_scan_ceiling("strncat", d, known, &ceiling)) return NULL;
    if (bounded_strlen(d, ceiling, &dlen)) return NULL;
    if (ceiling <= dlen) { errno = ERANGE; return NULL; }
    if (ceiling != (size_t)-1) {
        /* Payload bytes that still fit after d's terminator. Scan one byte
         * BEYOND the room, never just up to it: clamping the scan to `room`
         * made need = dlen + slen + 1 <= ceiling unconditional, so
         * check_capacity() always passed while the raw call below still got
         * the original n and wrote past d. A NUL inside the room still scans
         * to that NUL; a longer (or unterminated) source yields slen > room
         * and is refused before any write. */
        room = ceiling - dlen - 1u;
        scan_n = (n > room) ? room + 1u : n;
    } else {
        scan_n = n; /* no provable bound: legacy behaviour */
    }
    {
        size_t src_cap = source_scan_ceiling(s, scan_n);
        if (src_cap < scan_n) scan_n = src_cap;
    }
    slen = cplus_strnlen(s, scan_n);
    if (slen >= SIZE_MAX - dlen) { errno = EOVERFLOW; return NULL; }
    if (room != (size_t)-1 && slen > room) {
        cplus_libc_log("strncat", "overflow refused");
        errno = ERANGE;
        return NULL;
    }
    need = dlen + slen + 1u;
    if (check_capacity("strncat", d, need, known, &cap)) return NULL;
    /* Delegate only the prefix proven readable: a tracked source whose
     * capacity limited the scan must never make the raw loop read past it. */
    if (scan_n < n) n = scan_n;
    return cplus_strncat(d, s, n);
}
