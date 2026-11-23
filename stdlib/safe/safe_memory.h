#ifndef CPLUS_SAFE_MEMORY_H
#define CPLUS_SAFE_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(__cplusplus)
extern "C" {
#endif
#ifndef CPLUS_SAFE_ERRNO_T_DEFINED
typedef int errno_t;
#define CPLUS_SAFE_ERRNO_T_DEFINED
#endif
#ifndef RSIZE_MAX_MEM
#define RSIZE_MAX_MEM ((size_t)(SIZE_MAX >> 1))
#endif
#ifndef CPLUS_SAFE_RSIZE_T_DEFINED
typedef size_t rsize_t;
#define CPLUS_SAFE_RSIZE_T_DEFINED
#endif

/*
 * Checked memory adapters (safeclib-backed).
 *
 * Error contract: every function returns `errno_t`: 0 (== CPLUS_SAFE_OK) on
 * success, a non-zero safeclib constraint code on failure. The codes are NOT
 * the platform `errno` values, for example:
 *   ESNULLP (400) null pointer       ESZEROL (401) zero length
 *   ESLEMIN (402) length below min   ESLEMAX (403) size > RSIZE_MAX_MEM
 *   ESOVRLP (404) overlap undefined  ESNOSPC (406) destination too small
 * Test the result with `!= 0`; do NOT test `< 0` (the codes are positive).
 *
 * `cplus_memcmp_s` reports the ordering through the out-parameter `diff` and
 * returns an error code only for constraint violations. `cplus_memzero_s` keeps
 * zero-length calls defined.
 *
 * When a checked adapter has a PROVABLE destination capacity (a malloc_s /
 * calloc_s / realloc_s allocation, including an interior pointer, or a
 * compiler-visible object size), a caller-supplied `dmax` larger than that
 * capacity is refused early with ESNOSPC (406) and errno = ERANGE. This avoids
 * safeclib's constraint-error path, which clears `dmax` bytes of `dest` and
 * would itself overrun the object. On ERROR paths the adapters also publish a
 * standard errno value (EINVAL / ERANGE / EOVERFLOW) in addition to the safeclib
 * code; test the return code with `!= 0`, or errno with `!= 0`, never `< 0`.
 *
 * Honest boundary: raw `cplus_sc_*` entry points and destinations whose
 * capacity is unknown at the library layer (ordinary stack buffers, host
 * malloc) cannot be clamped here; see tests/defense/memory/HARDENING.md.
 *
 * Scope / honesty: these run runtime constraint checks (null, bounds, overlap,
 * max size). They do NOT prove pointer provenance, ownership, lifetime, or
 * alias/UAF safety — this is not Rust-grade safety, and those properties remain
 * the caller's and the compiler's responsibility.
 */
errno_t cplus_memcpy_s(void *dest, rsize_t dmax, const void *src, rsize_t smax);
errno_t cplus_memmove_s(void *dest, rsize_t dmax, const void *src, rsize_t smax);
errno_t cplus_memset_s(void *dest, rsize_t dmax, int value, rsize_t n);
errno_t cplus_memcmp_s(const void *dest, rsize_t dmax, const void *src, rsize_t smax, int *diff);
errno_t cplus_memzero_s(void *dest, rsize_t dmax);

/* Standard-C-signature auto-bounds-checked wrappers: use tracked malloc_s
 * allocation metadata and compile-time __builtin_object_size(p, 0) to catch
 * heap and stack overflows even when called with standard (dest, src, n). */
void *cplus_memcpy_s_auto(void *dest, const void *src, size_t n, size_t known_dmax);
void *cplus_memmove_s_auto(void *dest, const void *src, size_t n, size_t known_dmax);
void *cplus_memset_s_auto(void *dest, int value, size_t n, size_t known_dmax);
int   cplus_memcmp_s_auto(const void *s1, const void *s2, size_t n, size_t known_s1, size_t known_s2);

/* Unknown-capacity policy. 0 (default) = preserve raw-C behaviour but emit a
 * warning; 1 (strict) = refuse operations whose destination capacity cannot be
 * proven from tracked metadata or __builtin_object_size. */
void cplus_safe_set_unknown_capacity_mode(int strict);
int  cplus_safe_unknown_capacity_strict(void);

#ifndef CPLUS_SAFE_OBJ_SIZE
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_SAFE_OBJ_SIZE(p) __builtin_object_size((p), 1) /* mode 1: exact enclosing object; mode 0 leaks trailing struct bytes */
#else
#define CPLUS_SAFE_OBJ_SIZE(p) ((size_t)-1)
#endif
#endif

#ifndef CPLUS_SAFE_CAT2
#define CPLUS_SAFE_CAT2(a, b) a##b
#endif

#ifndef CPLUS_SAFE_CAT
#define CPLUS_SAFE_CAT(a, b) CPLUS_SAFE_CAT2(a, b)
#endif

#ifndef CPLUS_SAFE_NARG_
#define CPLUS_SAFE_NARG_(_1, _2, _3, _4, _5, N, ...) N
#endif

#ifndef CPLUS_SAFE_NARG
#define CPLUS_SAFE_NARG(...) \
    CPLUS_SAFE_NARG_(__VA_ARGS__, 5, 4, 3, 2, 1, 0)
#endif

#ifndef CPLUS_SAFE_NO_UNPREFIXED_MACROS
#define CPLUS_SAFE_MEMCPY_3(dest, src, n) \
    cplus_memcpy_s_auto((dest), (src), (n), CPLUS_SAFE_OBJ_SIZE(dest))
#define CPLUS_SAFE_MEMCPY_4(dest, dmax, src, smax) \
    cplus_memcpy_s((dest), (dmax), (src), (smax))
#undef memcpy_s
#define memcpy_s(...) \
    CPLUS_SAFE_CAT(CPLUS_SAFE_MEMCPY_, CPLUS_SAFE_NARG(__VA_ARGS__))(__VA_ARGS__)

#define CPLUS_SAFE_MEMMOVE_3(dest, src, n) \
    cplus_memmove_s_auto((dest), (src), (n), CPLUS_SAFE_OBJ_SIZE(dest))
#define CPLUS_SAFE_MEMMOVE_4(dest, dmax, src, smax) \
    cplus_memmove_s((dest), (dmax), (src), (smax))
#undef memmove_s
#define memmove_s(...) \
    CPLUS_SAFE_CAT(CPLUS_SAFE_MEMMOVE_, CPLUS_SAFE_NARG(__VA_ARGS__))(__VA_ARGS__)

#define CPLUS_SAFE_MEMSET_3(dest, val, n) \
    cplus_memset_s_auto((dest), (val), (n), CPLUS_SAFE_OBJ_SIZE(dest))
#define CPLUS_SAFE_MEMSET_4(dest, dmax, val, n) \
    cplus_memset_s((dest), (dmax), (val), (n))
#undef memset_s
#define memset_s(...) \
    CPLUS_SAFE_CAT(CPLUS_SAFE_MEMSET_, CPLUS_SAFE_NARG(__VA_ARGS__))(__VA_ARGS__)

#define CPLUS_SAFE_MEMCMP_3(s1, s2, n) \
    cplus_memcmp_s_auto((s1), (s2), (n), CPLUS_SAFE_OBJ_SIZE(s1), CPLUS_SAFE_OBJ_SIZE(s2))
#define CPLUS_SAFE_MEMCMP_5(s1, dmax, s2, smax, diff) \
    cplus_memcmp_s((s1), (dmax), (s2), (smax), (diff))
#undef memcmp_s
#define memcmp_s(...) \
    CPLUS_SAFE_CAT(CPLUS_SAFE_MEMCMP_, CPLUS_SAFE_NARG(__VA_ARGS__))(__VA_ARGS__)

#undef memzero_s
#define memzero_s(dest, dmax) cplus_memzero_s((dest), (dmax))
#endif
#if defined(__cplusplus)
}
#endif
#endif
