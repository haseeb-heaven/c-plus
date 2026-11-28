/* C+ hardened libc: authoritative status surface for the four remaining C23
 * interfaces that are not covered by the other C23 gap translation units:
 *
 *   timespec_getres  (<time.h>)
 *   mbrtoc8          (<uchar.h>)
 *   c8rtomb          (<uchar.h>)
 *   FE_TIESAWAY      (<fenv.h>)
 *
 * STATUS ON THE HOST USED FOR THIS TREE (probe 2026-11-23; /usr/bin/clang
 * Apple clang 16.0.0 (clang-1600.0.26.6), arm64-apple-darwin27.2.0, SDK
 * /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk == MacOSX15.2.sdk):
 *
 *   ALL FOUR ARE BLOCKED. None is declared by any installed SDK C header,
 *   none is exported by libSystem.B.dylib or libc++.dylib, and FE_TIESAWAY
 *   is not defined by <fenv.h>. No pinned vendored source for any of the
 *   four exists in this tree. See docs/libc/C23_REMAINDERS_CLOSURE.md for
 *   the exact compile errors, the full evidence and the unblock conditions.
 *
 * The four capability macros below are therefore 0 and this header emits
 * NO cplus_* declarations: declaring a function with no backend would only
 * move the failure from compile time to link time, and defining a rounding
 * constant that the host does not provide would be fabrication. When a host
 * and a compiled backend finally exist, the matching macro(s) must be
 * changed to 1 in the SAME change that adds the backend.
 *
 * ABI: this header is strictly additive -- macros only, no types, no
 * declarations, no symbols. It is safe to include from C99/C11/C17/C2x and
 * from C++ translation units.
 */
#ifndef CPLUS_C23_REMAINDERS_H
#define CPLUS_C23_REMAINDERS_H

/* 1 means "host declares AND exports the interface, and a cplus_* backend
 * is compiled into the libc". All four are 0 as of the probe above. */
#define CPLUS_C23_REMAINDERS_HAVE_TIMESPEC_GETRES 0
#define CPLUS_C23_REMAINDERS_HAVE_MBRTOC8         0
#define CPLUS_C23_REMAINDERS_HAVE_C8RTOMB         0
#define CPLUS_C23_REMAINDERS_HAVE_FE_TIESAWAY     0

/* Convenience aggregate for callers that need one answer. Must be updated
 * together with the four macros above; it is 1 only while none is closed. */
#define CPLUS_C23_REMAINDERS_ALL_BLOCKED 1

/* Machine-readable, single-sentence blocker for each interface. Tests print
 * these verbatim, so there is one authoritative description of each gap. */
#define CPLUS_C23_REMAINDERS_BLOCKER_TIMESPEC_GETRES                         \
    "timespec_getres: undeclared in every installed SDK <time.h> "            \
    "(error: call to undeclared function 'timespec_getres'), no libSystem "   \
    "symbol, no pinned upstream source; no algorithm fabricated"
#define CPLUS_C23_REMAINDERS_BLOCKER_MBRTOC8                                 \
    "mbrtoc8: no host <uchar.h>, no char8_t in C modes, no mbrtoc8 symbol "   \
    "in any installed SDK or dylib, no pinned upstream source; no "          \
    "conversion algorithm fabricated"
#define CPLUS_C23_REMAINDERS_BLOCKER_C8RTOMB                                 \
    "c8rtomb: no host <uchar.h>, no char8_t in C modes, no c8rtomb symbol "   \
    "in any installed SDK or dylib, no pinned upstream source; no "          \
    "conversion algorithm fabricated"
#define CPLUS_C23_REMAINDERS_BLOCKER_FE_TIESAWAY                             \
    "FE_TIESAWAY: <fenv.h> defines only FE_TONEAREST/FE_UPWARD/FE_DOWNWARD/"  \
    "FE_TOWARDZERO (error: use of undeclared identifier 'FE_TIESAWAY'); "     \
    "no numeric value fabricated"

/*
 * Intended C23 signatures, recorded for a future implementation as COMMENT
 * ONLY -- they are deliberately not active declarations on this host:
 *
 *   int timespec_getres(struct timespec *ts, int base);
 *       -> base on success, 0 on failure; writes the resolution of `base`.
 *   size_t mbrtoc8(char8_t *restrict pc8, const char *restrict s,
 *                  size_t n, mbstate_t *restrict ps);
 *   size_t c8rtomb(char *restrict s, char8_t c8, mbstate_t *restrict ps);
 *       -> mirror mbrtoc16/c16rtomb: (size_t)-1 + EILSEQ on error, -2
 *          incomplete input, -3 buffered output, 0 a null character.
 *   FE_TIESAWAY is a rounding-mode macro used by fesetround/fegetround;
 *   it has no independent numeric definition to derive on this host.
 *
 * NOTE: char8_t is owned by the <uchar.h> compatibility surface
 * (libc/install-include/cplus_uchar.h); this header must not typedef it,
 * otherwise the two headers would conflict when included together.
 */
#endif /* CPLUS_C23_REMAINDERS_H */
