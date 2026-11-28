/*
 * c23_small_gaps.c -- small C23 API gaps: memset_explicit (<string.h>),
 * timespec_getres (<time.h>) and FE_TIESAWAY (<fenv.h>).
 *
 * POLICY (see docs/libc/ISO_C_FULL_INVENTORY.md): HOST FIRST, THEN VERBATIM
 * VENDORED UPSTREAM, OTHERWISE REPORT THE EXACT BLOCKER. This file never
 * invents an algorithm and never invents a rounding-mode constant. Every
 * wrapper below is compiled ONLY when the host runtime actually declares the
 * corresponding C23 symbol; otherwise the capability macro is 0 and no
 * substitute is emitted. FE_TIESAWAY is a macro, so it can only be published
 * when the host <fenv.h> defines it.
 *
 * ---------------------------------------------------------------------------
 * HOST PROBE (2026-11-22): /usr/bin/clang -std=c17, Apple clang 16.0.0
 * (clang-1600.0.26.6), target arm64-apple-darwin27.2.0, CommandLineTools
 * SDK 15.2 (/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk).
 *
 *   #include <string.h>  -> memset_explicit(b, 0, sizeof b)
 *       error: call to undeclared function 'memset_explicit'
 *   #include <time.h>    -> timespec_getres(&ts, TIME_UTC)
 *       error: call to undeclared function 'timespec_getres'
 *       note: did you mean 'timespec_get'?
 *   #include <fenv.h>    -> return FE_TIESAWAY;
 *       error: use of undeclared identifier 'FE_TIESAWAY'
 *
 * All three probes also fail with -std=c2x. An SDK-wide header grep finds no
 * declaration/macro, and libSystem.B.dylib exports none of the three symbols
 * (nor an explicit_bzero fallback). <fenv.h> here defines only FE_TONEAREST,
 * FE_UPWARD, FE_DOWNWARD and FE_TOWARDZERO -- there is no ties-away value to
 * read, so defining FE_TIESAWAY would be fabrication.
 *
 * VENDORED SOURCE CHECK: libc/upstream/musl (commit
 * 7813e9119eb5fe681b6750b8a033a0490bca0941) ships only src/string/memset.c,
 * which is plain memset, not memset_explicit; it has no timespec_getres.c and
 * no <fenv.h> at all. Therefore there is no vendored upstream body available
 * to copy verbatim either.
 *
 * EXACT BLOCKER: this host's C library and SDK expose none of the three C23
 * interfaces (no symbol, no declaration, no macro) and the pinned vendored
 * musl snapshot contains no corresponding source. The three capabilities are
 * therefore reported as UNAVAILABLE and the tests SKIP with this note. No
 * hand-written implementation, compiler barrier trick or made-up constant is
 * provided in their place.
 * ---------------------------------------------------------------------------
 */

/* Feature macros precede system headers: glibc declares the C23 functions
 * only when its full feature set is enabled. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#include "../log/log.h"
#include <errno.h>
#include <fenv.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

/*
 * Capability detection. glibc 2.38 is the first release that declares the
 * C23 functions memset_explicit() and timespec_getres(); on those hosts the
 * wrappers below simply delegate to the host symbol (the single source of
 * truth) and log entry per the C+ convention. The CPLUS_C23_HOST_HAS_*
 * overrides are OPT-IN build flags for other libc implementations whose
 * headers declare the symbol but expose no usable version macro; enabling
 * one on a host without the declaration fails the build loudly rather than
 * silently substituting an implementation.
 */
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#  if __GLIBC_PREREQ(2, 38)
#    define CPLUS_C23_HAVE_MEMSET_EXPLICIT 1
#    define CPLUS_C23_HAVE_TIMESPEC_GETRES 1
#  endif
#endif
#if defined(CPLUS_C23_HOST_HAS_MEMSET_EXPLICIT)
#  define CPLUS_C23_HAVE_MEMSET_EXPLICIT 1
#endif
#if defined(CPLUS_C23_HOST_HAS_TIMESPEC_GETRES)
#  define CPLUS_C23_HAVE_TIMESPEC_GETRES 1
#endif
#if !defined(CPLUS_C23_HAVE_MEMSET_EXPLICIT)
#  define CPLUS_C23_HAVE_MEMSET_EXPLICIT 0
#endif
#if !defined(CPLUS_C23_HAVE_TIMESPEC_GETRES)
#  define CPLUS_C23_HAVE_TIMESPEC_GETRES 0
#endif

/* FE_TIESAWAY is a macro, not a function: it exists only if the host <fenv.h>
 * provides it. The local alias makes the capability explicit; publishing it
 * from the installed public header is only meaningful on such a host. */
#if defined(FE_TIESAWAY)
#  define CPLUS_C23_HAVE_FE_TIESAWAY 1
#  define CPLUS_FE_TIESAWAY FE_TIESAWAY
#else
#  define CPLUS_C23_HAVE_FE_TIESAWAY 0
#endif

#if CPLUS_C23_HAVE_MEMSET_EXPLICIT
/* C23 memset_explicit(): host-owned anti-optimisation semantics. The NULL
 * guard only gives defined behaviour for an input that is UB for the ISO C
 * function; all valid inputs pass through unchanged. */
void *cplus_memset_explicit(void *s, int c, size_t n) {
    cplus_libc_log("memset_explicit", "enter");
    if (!s) { errno = EINVAL; return s; }
    return memset_explicit(s, c, n);
}
#endif /* CPLUS_C23_HAVE_MEMSET_EXPLICIT */

#if CPLUS_C23_HAVE_TIMESPEC_GETRES
/* C23 timespec_getres(): resolution of `base` written to *ts; returns `base`
 * on success and 0 on failure (host-defined). NULL out-param is rejected. */
int cplus_timespec_getres(struct timespec *ts, int base) {
    cplus_libc_log("timespec_getres", "enter");
    if (!ts) { errno = EINVAL; return 0; }
    return timespec_getres(ts, base);
}
#endif /* CPLUS_C23_HAVE_TIMESPEC_GETRES */
