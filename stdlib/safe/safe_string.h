#ifndef CPLUS_SAFE_STRING_H
#define CPLUS_SAFE_STRING_H
#include <stddef.h>
#include <stdint.h>
#if defined(__cplusplus)
extern "C" {
#endif
#ifndef CPLUS_SAFE_ERRNO_T_DEFINED
typedef int errno_t;
#define CPLUS_SAFE_ERRNO_T_DEFINED
#endif
#ifndef RSIZE_MAX_STR
#define RSIZE_MAX_STR ((size_t)(SIZE_MAX >> 1))
#endif
#ifndef CPLUS_SAFE_RSIZE_T_DEFINED
typedef size_t rsize_t;
#define CPLUS_SAFE_RSIZE_T_DEFINED
#endif

/*
 * Checked string adapters (safeclib-backed).
 *
 * Error contract: the `errno_t`-returning functions return 0 (== CPLUS_SAFE_OK)
 * on success and a non-zero code on failure. The codes are safeclib constraint
 * codes, which are NOT the platform `errno` values, for example:
 *   ESNULLP (400) null pointer       ESZEROL (401) zero length
 *   ESLEMIN (402) length below min   ESLEMAX (403) size > RSIZE_MAX_STR
 *   ESOVRLP (404) overlap undefined  ESNOSPC (406) destination too small
 *   ESUNTERM (407) unterminated string
 * Test the result with `!= 0`; do NOT test `< 0` (the codes are positive).
 *
 * Non-`errno_t` members: `cplus_strnlen_s` is a bounded length query returning a
 * count in [0, smax]; `cplus_strtok_s` returns the next token pointer or NULL.
 *
 * Scope / honesty: these run runtime constraint checks (null, bounds, overlap,
 * max size). They do NOT prove pointer provenance, ownership, lifetime, or
 * alias/UAF safety — this is not Rust-grade safety, and those properties remain
 * the caller's and the compiler's responsibility.
 */
errno_t cplus_strcpy_s(char *dest, rsize_t dmax, const char *src);
errno_t cplus_strncpy_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);
errno_t cplus_strcat_s(char *dest, rsize_t dmax, const char *src);
errno_t cplus_strncat_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);
errno_t cplus_strcmp_s(const char *dest, rsize_t dmax, const char *src, int *indicator);
rsize_t cplus_strnlen_s(const char *str, rsize_t smax);
char *cplus_strtok_s(char *dest, rsize_t *dmax, const char *delim, char **ptr);
errno_t cplus_strerror_s(char *dest, rsize_t dmax, int errnum);
#if defined(__cplusplus)
}
#endif
#endif
