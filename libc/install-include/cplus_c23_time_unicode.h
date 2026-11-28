/* C+ hardened libc: standalone public surface for three C23 additions that
 * are MISSING from docs/libc/iso_c_inventory.json:
 *
 *   timespec_getres  (<time.h>)  -- C23 resolution query
 *   mbrtoc8          (<uchar.h>)  -- C23 UTF-8 multibyte -> char8_t decode
 *   c8rtomb          (<uchar.h>)  -- C23 char8_t -> multibyte encode
 *
 * STATUS: IMPLEMENTED (from-standard implementation, NOT vendored upstream).
 * This host (/usr/bin/clang, Apple clang 16.0.0, arm64-apple-darwin27.2.0,
 * MacOSX.sdk == MacOSX15.2.sdk) declares none of the three functions and
 * exports none of them; see docs/libc/C23_TIME_UNICODE.md for the exact probe
 * commands, the clock_getres evidence and the provenance/honesty statement.
 *
 * The declarations below have the EXACT C23 signature shape (return types,
 * parameter types and restrict qualifiers); only the public name carries the
 * project-wide cplus_* prefix used by every other API in this repository.
 *
 * char8_t is C23's distinct unsigned 8-bit character type. In C modes this
 * header models it as `unsigned char` (the same model used by
 * libc/install-include/cplus_uchar.h). In C++ modes the built-in char8_t is
 * used when available; otherwise the same unsigned-char model applies.
 *
 * ABI: self-contained. Includes only <stddef.h>, <time.h> and <wchar.h>; it
 * does NOT include the host <uchar.h> (absent on this SDK) and does NOT depend
 * on cplus_uchar.h. Safe to include from C99/C11/C17/C2x and C++ TUs.
 */
#ifndef CPLUS_C23_TIME_UNICODE_H
#define CPLUS_C23_TIME_UNICODE_H

#include <stddef.h> /* size_t */
#include <time.h>   /* struct timespec, TIME_UTC */
#include <wchar.h>  /* mbstate_t */

#ifdef __cplusplus
extern "C" {
#endif

/* --- char8_t model -------------------------------------------------------- */
#ifndef CPLUS_C23_TU_CHAR8_T_DEFINED
#  ifdef __cplusplus
#    if __cplusplus < 202002L
       /* Pre-C++20 has no built-in char8_t. */
typedef unsigned char char8_t;
#      define CPLUS_C23_TU_CHAR8_T_DEFINED 1
#    else
#      define CPLUS_C23_TU_CHAR8_T_DEFINED 1 /* built-in char8_t */
#    endif
#  else
#    if defined(CPLUS_UCHAR_H) && defined(__STDC_VERSION__) &&                 \
        __STDC_VERSION__ >= 202000L
       /* cplus_uchar.h already defined char8_t for this C2x-or-later TU. */
#      define CPLUS_C23_TU_CHAR8_T_DEFINED 1
#    else
typedef unsigned char char8_t;
#      define CPLUS_C23_TU_CHAR8_T_DEFINED 1
#    endif
#  endif
#endif

/* 'restrict' is a C keyword, not valid in C++. */
#if defined(__cplusplus)
#  define CPLUS_C23_TU_RESTRICT
#else
#  define CPLUS_C23_TU_RESTRICT restrict
#endif

/* --- C23 <time.h> --------------------------------------------------------- */
/* Resolution of `base` written to *ts. Returns `base` on success, 0 on
 * failure. ts == NULL is rejected with errno == EINVAL. For TIME_UTC on this
 * host the resolution is {0, 1000} (1 microsecond), corroborated by
 * clock_getres(CLOCK_REALTIME) and by a 200000-call timespec_get probe. */
int cplus_timespec_getres(struct timespec *ts, int base);

/* --- C23 <uchar.h> -------------------------------------------------------- */
/* Return conventions mirror mbrtoc16/c16rtomb:
 *   (size_t)-1 + errno == EILSEQ : encoding error
 *   (size_t)-2                   : incomplete but so-far-valid input
 *   (size_t)-3                   : next buffered UTF-8 code unit in *pc8
 *                                  (no input consumed)
 *   0                            : null character (*pc8 set to 0)
 *   otherwise                    : number of input bytes consumed (mbrtoc8)
 *                                  / number of multibyte bytes stored
 *                                  (c8rtomb).
 * ps == NULL selects a dedicated internal state object. */
size_t cplus_mbrtoc8(char8_t *CPLUS_C23_TU_RESTRICT pc8,
                     const char *CPLUS_C23_TU_RESTRICT s,
                     size_t n,
                     mbstate_t *CPLUS_C23_TU_RESTRICT ps);
size_t cplus_c8rtomb(char *CPLUS_C23_TU_RESTRICT s,
                     char8_t c8,
                     mbstate_t *CPLUS_C23_TU_RESTRICT ps);

#undef CPLUS_C23_TU_RESTRICT

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_C23_TIME_UNICODE_H */
