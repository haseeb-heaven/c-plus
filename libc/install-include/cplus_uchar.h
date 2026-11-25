/* C+ hardened libc: self-contained <uchar.h> compatibility surface.
 *
 * This header intentionally does NOT include the host <uchar.h> (Apple Clang 16
 * on macOS provides none). It supplies the three standard character types and
 * the cplus_* API declarations for the ISO C11 UTF-16/UTF-32 conversion family.
 *
 * IMPORTANT -- DECLARATIONS ONLY, NO BACKEND CURRENTLY SHIPPED:
 *   The six conversion entry points (cplus_mbrtoc16/c16rtomb/mbrtoc32/
 *   c32rtomb and the C23 cplus_mbrtoc8/c8rtomb) are declared here so the API
 *   surface is visible, but they are NOT implemented in libc/src/uchar/uchar.c.
 *   Calling one produces a link error until a backend is supplied. See
 *   libc/src/uchar/uchar.c for the exact probe evidence and blocker.
 *   The C23 c8 variants are not even declared here because the host provides
 *   no such symbols and no vendored implementation is available.
 *
 * The typedefs below, by contrast, are complete and usable today.
 */
#ifndef CPLUS_UCHAR_H
#define CPLUS_UCHAR_H

#include <stddef.h>  /* size_t */
#include <stdint.h>  /* uint_least16_t, uint_least32_t */
#include <wchar.h>   /* mbstate_t */

#ifdef __cplusplus
extern "C" {
#endif

/* C++11 and later provide char16_t/char32_t (and C++20 char8_t) as built-in
 * types, so the typedefs are emitted for C translation units only. */
#ifndef __cplusplus
typedef uint_least16_t char16_t;
typedef uint_least32_t char32_t;

/* C23 added char8_t as a distinct unsigned character type. Apple clang 16
 * has no -std=c23 spelling: its C2x draft mode (-std=c2x) reports
 * __STDC_VERSION__ 202000L, so accept that value as well as the final C23
 * 202311L value. CPLUS_UCHAR_HAS_CHAR8_T lets tests detect the typedef. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202000L
#define CPLUS_UCHAR_HAS_CHAR8_T 1
typedef unsigned char char8_t;
#endif
#endif

/* 'restrict' is a C keyword, not valid in C++ mode. */
#if defined(__cplusplus)
#define CPLUS_UCHAR_RESTRICT
#else
#define CPLUS_UCHAR_RESTRICT restrict
#endif

/* --- ISO C11 <uchar.h> conversion surface (DECLARED, NOT IMPLEMENTED) ------
 * Contracts mirror mbrtoc16/c16rtomb/mbrtoc32/c32rtomb: a return of (size_t)-1
 * denotes an encoding error (errno == EILSEQ), -2 an incomplete but so-far
 * valid multibyte sequence, -3 a stored surrogate under mbrtoc16, and 0 a
 * null wide character. ps == NULL selects the host internal state.
 */
size_t cplus_mbrtoc16(char16_t *CPLUS_UCHAR_RESTRICT pc16,
                      const char *CPLUS_UCHAR_RESTRICT s,
                      size_t n, mbstate_t *CPLUS_UCHAR_RESTRICT ps);
size_t cplus_c16rtomb(char *CPLUS_UCHAR_RESTRICT s,
                      char16_t c16, mbstate_t *CPLUS_UCHAR_RESTRICT ps);
size_t cplus_mbrtoc32(char32_t *CPLUS_UCHAR_RESTRICT pc32,
                      const char *CPLUS_UCHAR_RESTRICT s,
                      size_t n, mbstate_t *CPLUS_UCHAR_RESTRICT ps);
size_t cplus_c32rtomb(char *CPLUS_UCHAR_RESTRICT s,
                      char32_t c32, mbstate_t *CPLUS_UCHAR_RESTRICT ps);

#undef CPLUS_UCHAR_RESTRICT

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_UCHAR_H */
