#ifndef CPLUS_SAFE_STRING_H
#define CPLUS_SAFE_STRING_H
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
 * For `strnlen_s`, an out-of-range `smax` (0 or > RSIZE_MAX_STR) is a defined
 * refusal that returns 0; a terminating NUL is never sought past `smax`.
 *
 * The standard-C-signature `_auto` wrappers derive a hard scan ceiling from
 * the destination's provable capacity and use bounded length queries, so an
 * unterminated source/destination is refused (NULL + ERANGE) instead of being
 * over-read.  Valid-input behaviour is unchanged.
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

/* Standard-C-signature auto-bounds-checked wrappers */
char *cplus_strcpy_s_auto(char *dest, const char *src, size_t known_dmax);
char *cplus_strncpy_s_auto(char *dest, const char *src, size_t n, size_t known_dmax);
char *cplus_strcat_s_auto(char *dest, const char *src, size_t known_dmax);
char *cplus_strncat_s_auto(char *dest, const char *src, size_t n, size_t known_dmax);

#ifndef CPLUS_SAFE_OBJ_SIZE
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_SAFE_OBJ_SIZE(p) __builtin_object_size((p), 1) /* mode 1: exact enclosing object; mode 0 leaks trailing struct bytes */
#else
#define CPLUS_SAFE_OBJ_SIZE(p) ((size_t)-1)
#endif
#define CPLUS_SAFE_CAT2(a, b) a##b
#define CPLUS_SAFE_CAT(a, b) CPLUS_SAFE_CAT2(a, b)
#define CPLUS_SAFE_NARG_(_1, _2, _3, _4, _5, N, ...) N
#define CPLUS_SAFE_NARG(...) CPLUS_SAFE_NARG_(__VA_ARGS__, 5, 4, 3, 2, 1, 0)
#endif

#ifndef CPLUS_SAFE_NO_UNPREFIXED_MACROS
#define CPLUS_SAFE_STRCPY_2(dest, src) \
    cplus_strcpy_s_auto((dest), (src), CPLUS_SAFE_OBJ_SIZE(dest))
#define CPLUS_SAFE_STRCPY_3(dest, dmax, src) \
    cplus_strcpy_s((dest), (dmax), (src))
#undef strcpy_s
#define strcpy_s(...) \
    CPLUS_SAFE_CAT(CPLUS_SAFE_STRCPY_, CPLUS_SAFE_NARG(__VA_ARGS__))(__VA_ARGS__)

#define CPLUS_SAFE_STRNCPY_3(dest, src, n) \
    cplus_strncpy_s_auto((dest), (src), (n), CPLUS_SAFE_OBJ_SIZE(dest))
#define CPLUS_SAFE_STRNCPY_4(dest, dmax, src, slen) \
    cplus_strncpy_s((dest), (dmax), (src), (slen))
#undef strncpy_s
#define strncpy_s(...) \
    CPLUS_SAFE_CAT(CPLUS_SAFE_STRNCPY_, CPLUS_SAFE_NARG(__VA_ARGS__))(__VA_ARGS__)

#define CPLUS_SAFE_STRCAT_2(dest, src) \
    cplus_strcat_s_auto((dest), (src), CPLUS_SAFE_OBJ_SIZE(dest))
#define CPLUS_SAFE_STRCAT_3(dest, dmax, src) \
    cplus_strcat_s((dest), (dmax), (src))
#undef strcat_s
#define strcat_s(...) \
    CPLUS_SAFE_CAT(CPLUS_SAFE_STRCAT_, CPLUS_SAFE_NARG(__VA_ARGS__))(__VA_ARGS__)

#define CPLUS_SAFE_STRNCAT_3(dest, src, n) \
    cplus_strncat_s_auto((dest), (src), (n), CPLUS_SAFE_OBJ_SIZE(dest))
#define CPLUS_SAFE_STRNCAT_4(dest, dmax, src, slen) \
    cplus_strncat_s((dest), (dmax), (src), (slen))
#undef strncat_s
#define strncat_s(...) \
    CPLUS_SAFE_CAT(CPLUS_SAFE_STRNCAT_, CPLUS_SAFE_NARG(__VA_ARGS__))(__VA_ARGS__)

#undef strnlen_s
#define strnlen_s cplus_strnlen_s
#undef strerror_s
#define strerror_s cplus_strerror_s
#undef strtok_s
#define strtok_s cplus_strtok_s
#endif
#if defined(__cplusplus)
}
#endif
#endif
