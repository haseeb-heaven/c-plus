#ifndef CPLUS_SAFE_MEMORY_H
#define CPLUS_SAFE_MEMORY_H
#include <stddef.h>
#include <stdint.h>
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
#if defined(__cplusplus)
}
#endif
#endif
