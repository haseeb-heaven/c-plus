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
#include <string.h>

static int g_unknown_strict = 0;

void cplus_libc_set_unknown_capacity_mode(int strict) {
    g_unknown_strict = strict ? 1 : 0;
}

static size_t resolve_capacity(const void *p, size_t known) {
    size_t tracked = cplus_alloc_remaining_capacity(p);
    if (tracked != (size_t)-1) return tracked;
    if (known != (size_t)-1) return known;
    return (size_t)-1;
}

/* Returns 1 when the call must be refused, 0 otherwise. */
static int check_capacity(const char *api, const void *p, size_t need,
                          size_t known, size_t *cap_out) {
    size_t cap = resolve_capacity(p, known);
    if (cap == (size_t)-1) {
        if (g_unknown_strict) {
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

void *cplus_memset_bounded(void *d, int v, size_t n, size_t known) {
    size_t cap = 0;
    if (!d) { errno = EINVAL; return NULL; }
    if (check_capacity("memset", d, n, known, &cap)) return NULL;
    return cplus_memset(d, v, n);
}

void *cplus_memcpy_bounded(void *d, const void *s, size_t n, size_t known) {
    size_t cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    if (check_capacity("memcpy", d, n, known, &cap)) return NULL;
    return cplus_memcpy(d, s, n);
}

void *cplus_memmove_bounded(void *d, const void *s, size_t n, size_t known) {
    size_t cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    if (check_capacity("memmove", d, n, known, &cap)) return NULL;
    return cplus_memmove(d, s, n);
}

int cplus_memcmp_bounded(const void *a, const void *b, size_t n,
                         size_t known_a, size_t known_b) {
    size_t cap = 0;
    if (!a || !b) { errno = EINVAL; return 0; }
    if (check_capacity("memcmp(a)", a, n, known_a, &cap)) return 0;
    if (check_capacity("memcmp(b)", b, n, known_b, &cap)) return 0;
    return cplus_memcmp(a, b, n);
}

char *cplus_strcpy_bounded(char *d, const char *s, size_t known) {
    size_t need, cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    need = cplus_strlen(s) + 1u; /* unterminated source remains caller UB */
    if (check_capacity("strcpy", d, need, known, &cap)) return NULL;
    return cplus_strcpy(d, s);
}

char *cplus_strncpy_bounded(char *d, const char *s, size_t n, size_t known) {
    size_t cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    if (check_capacity("strncpy", d, n, known, &cap)) return NULL;
    return cplus_strncpy(d, s, n);
}

char *cplus_strcat_bounded(char *d, const char *s, size_t known) {
    size_t need, cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    need = cplus_strlen(d) + cplus_strlen(s) + 1u;
    if (check_capacity("strcat", d, need, known, &cap)) return NULL;
    return cplus_strcat(d, s);
}

char *cplus_strncat_bounded(char *d, const char *s, size_t n, size_t known) {
    size_t need, cap = 0;
    if (!d || !s) { errno = EINVAL; return NULL; }
    need = cplus_strlen(d) + (n < cplus_strlen(s) ? n : cplus_strlen(s)) + 1u;
    if (check_capacity("strncat", d, need, known, &cap)) return NULL;
    return cplus_strncat(d, s, n);
}
