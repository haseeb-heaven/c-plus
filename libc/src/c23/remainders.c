/*
 * remainders.c -- the four remaining C23 interfaces: all BLOCKED, no body.
 *
 * Scope (companion to libc/src/c23_small_gaps.c and
 * libc/src/c23_upstream_small.c): timespec_getres (<time.h>), mbrtoc8 and
 * c8rtomb (<uchar.h>), FE_TIESAWAY (<fenv.h>).
 *
 * POLICY: HOST FIRST, THEN VERBATIM PINNED UPSTREAM, OTHERWISE REPORT THE
 * EXACT BLOCKER. No algorithm is written from scratch, no rounding-mode
 * constant is invented, and no declaration is published without a backend.
 * Consequently this translation unit deliberately emits NO externally
 * visible code or data; it exists to carry the probe record in one place and
 * to fail loudly if the authoritative status header is changed without a
 * corresponding backend change.
 *
 * ---------------------------------------------------------------------------
 * HOST PROBES (2026-11-23), compiled from stdin with:
 *   /usr/bin/clang -std=c11 -isysroot \
 *     /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -x c - -o /dev/null
 *
 *   #include <time.h>
 *   int main(void){ struct timespec ts;
 *                   return timespec_getres(&ts, TIME_UTC); }
 *     -std=c11/c17:
 *       error: call to undeclared function 'timespec_getres'; ISO C99 and
 *       later do not support implicit function declarations
 *       [-Wimplicit-function-declaration]
 *       note: did you mean 'timespec_get'? (_time.h:199)
 *     -std=c2x:
 *       error: use of undeclared identifier 'timespec_getres'; did you mean
 *       'timespec_get'?
 *
 *   #include <fenv.h>
 *   int main(void){ return FE_TIESAWAY; }
 *     -std=c11/c17/c2x:
 *       error: use of undeclared identifier 'FE_TIESAWAY'
 *     <fenv.h> defines FE_TONEAREST (0), FE_UPWARD (0x00400000),
 *     FE_DOWNWARD (0x00800000) and FE_TOWARDZERO (0x00C00000) only. There
 *     is no ties-away encoding to read, so publishing a number is impossible
 *     without fabrication.
 *
 *   #include <wchar.h>
 *   int main(void){ char8_t c8; mbstate_t ps = {0};
 *                   return (int)mbrtoc8(&c8, "x", 1, &ps); }
 *     -std=c11/c17:
 *       error: unknown type name 'char8_t'; did you mean 'wchar_t'?
 *       error: call to undeclared function 'mbrtoc8'; ISO C99 and later do
 *       not support implicit function declarations
 *       note: did you mean 'mbrtowc'? (_wchar.h:108)
 *     -std=c2x:
 *       error: unknown type name 'char8_t'; did you mean 'wchar_t'?
 *       error: use of undeclared identifier 'mbrtoc8'
 *
 *   #include <wchar.h>
 *   int main(void){ char buf[8]; mbstate_t ps = {0};
 *                   return (int)c8rtomb(buf, (char8_t)'x', &ps); }
 *     -std=c11/c17:
 *       error: call to undeclared function 'c8rtomb'; ISO C99 and later do
 *       not support implicit function declarations
 *       note: did you mean 'wcrtomb'? (_wchar.h:123)
 *       error: use of undeclared identifier 'char8_t'
 *     -std=c2x:
 *       error: use of undeclared identifier 'char8_t'
 *       error: use of undeclared identifier 'c8rtomb'
 *
 * SUPPLEMENTARY EVIDENCE:
 *   - SDK-wide grep: no mbrtoc8/c8rtomb/timespec_getres/FE_TIESAWAY in any
 *     installed SDK C header (MacOSX.sdk, MacOSX13.3.sdk, MacOSX14.4.sdk,
 *     MacOSX14.5.sdk, MacOSX15.sdk, MacOSX15.2.sdk). The only textual hits
 *     are in libc++ headers <cuchar>/<uchar.h>, inside a documentation
 *     comment; the real `using ::mbrtoc8` is guarded by
 *     _LIBCPP_HAS_NO_C8RTOMB_MBRTOC8, which __config always defines on this
 *     non-glibc host. A C++20 probe fails with:
 *       error: no member named 'mbrtoc8' in namespace 'std'
 *   - nm -gU: none of the four symbols is exported by libSystem.B.dylib or
 *     libc++.dylib, and none appears in any SDK .tbd file.
 *   - The host has no <uchar.h> at all in its C header directory.
 *   - Vendored tree libc/upstream/musl (pinned commit
 *     7813e9119eb5fe681b6750b8a033a0490bca0941) has no timespec_getres
 *     source, no UTF-8 conversion source and no fenv header. Earlier audits
 *     in docs/libc/C23_SMALL_UPSTREAM.md and docs/libc/UCHARC23_UPSTREAM.md
 *     also found no pinned LLVM libc/musl source for these four interfaces.
 *
 * UNBLOCK CONDITIONS (exact):
 *   timespec_getres: a host whose <time.h> declares it and whose runtime
 *     exports it (e.g. glibc >= 2.38), or a verbatim pinned upstream body;
 *     then add a distinct cplus_* backend and set the matching header macro.
 *     Coordinate with libc/src/c23_small_gaps.c, which already defines
 *     cplus_timespec_getres on such a host, to avoid a duplicate symbol.
 *   mbrtoc8 / c8rtomb: host symbols (glibc >= 2.36) or a pinned upstream
 *     revision that actually ships both bodies. The existing 2023 musl-ml
 *     patch is unmerged and needs non-mechanical host adaptation, so it is
 *     not eligible under the verbatim-source rule.
 *   FE_TIESAWAY: the host <fenv.h> (or the pinned compiler header) must
 *     define the macro; only then may a local alias be published.
 * ---------------------------------------------------------------------------
 */

#include "../../install-include/cplus_c23_remainders.h"

/* Keep this a well-formed ISO C translation unit without emitting a symbol. */
typedef int cplus_c23_remainders_blocker_marker;

/* If the status header ever changes, force a review of this file (backend,
 * logging and tests) instead of silently claiming an API is now available. */
typedef char cplus_c23_remainders_status_must_still_be_all_blocked[
    CPLUS_C23_REMAINDERS_ALL_BLOCKED ? 1 : -1];
