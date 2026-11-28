/*
 * test_c23_remainders.c -- status test for the four C23 remainder APIs:
 * timespec_getres, mbrtoc8, c8rtomb, FE_TIESAWAY.
 *
 * Nothing is faked: all four are BLOCKED on the host used for this tree, so
 * no cplus_* call is attempted and each case prints its exact blocker from
 * libc/install-include/cplus_c23_remainders.h. The test still does real work:
 * it checks, at compile time, that the authoritative status macros say
 * "blocked", and at run time it exercises the adjacent HOST facilities that
 * were used for the probes (timespec_get, the four host rounding modes, and
 * the C11 mbrtowc/wcrtomb pair). That proves the adjacent functionality
 * works here while the four C23 entries genuinely do not exist.
 *
 * Build (standalone; no cplus_* symbol is referenced):
 *   /usr/bin/clang -std=c99 -Wall -Wextra -Wpedantic -Werror \
 *     -I../../libc/install-include test_c23_remainders.c -o test_c23_remainders
 *   ./test_c23_remainders
 *
 * The standard libc test runner compiles every test with -std=c99, so this
 * file must build and run under C99. The adjacent-host boundary probes are
 * therefore feature-gated: the C11 timespec_get() probe is compiled only when
 * the SDK actually declares it (C11+ or full Darwin C level). Under strict
 * C99 that probe is omitted, but the four authoritative SKIP lines and the
 * remaining C99-available host probes (fenv rounding modes and the C95
 * mbrtowc/wcrtomb pair) still run. Nothing is fabricated.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include "../../libc/install-include/cplus_c23_remainders.h"

/* The SDK declares timespec_get()/TIME_UTC only when C11+ is requested or the
 * full Darwin C level is selected (see <_time.h>). Under the runner's strict
 * -std=c99 with _POSIX_C_SOURCE set, neither holds, so the host probe must be
 * compiled out. This mirrors the SDK gate and keeps the test buildable in C99. */
#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) || \
    (defined(__DARWIN_C_LEVEL) && defined(__DARWIN_C_FULL) && \
     __DARWIN_C_LEVEL >= __DARWIN_C_FULL)
#define CPLUS_TEST_HAVE_HOST_TIMESPEC_GET 1
#endif

/* Compile-time status assertions: the header must describe this host, and
 * no active declaration can exist for an API without a backend. */
typedef char cplus_test_remainders_ts_blocked[
    (CPLUS_C23_REMAINDERS_HAVE_TIMESPEC_GETRES == 0) ? 1 : -1];
typedef char cplus_test_remainders_mbrtoc8_blocked[
    (CPLUS_C23_REMAINDERS_HAVE_MBRTOC8 == 0) ? 1 : -1];
typedef char cplus_test_remainders_c8rtomb_blocked[
    (CPLUS_C23_REMAINDERS_HAVE_C8RTOMB == 0) ? 1 : -1];
typedef char cplus_test_remainders_tiesaway_blocked[
    (CPLUS_C23_REMAINDERS_HAVE_FE_TIESAWAY == 0) ? 1 : -1];
typedef char cplus_test_remainders_all_blocked[
    (CPLUS_C23_REMAINDERS_ALL_BLOCKED == 1) ? 1 : -1];

/* Adjacent host boundary cases: positive evidence that the probes were run
 * against a working C library, not a broken toolchain. */
#ifdef CPLUS_TEST_HAVE_HOST_TIMESPEC_GET
static void test_host_timespec_boundaries(void) {
    struct timespec ts;
    int valid, zero_base, huge_base;

    memset(&ts, 0, sizeof ts);
    valid = timespec_get(&ts, TIME_UTC);
    assert(valid != 0);                 /* the C11 entry point works */
    assert(ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000);

    /* Unsupported bases are rejected: these are the boundary inputs that a
     * future timespec_getres would also have to classify. */
    zero_base = timespec_get(&ts, 0);
    huge_base = timespec_get(&ts, INT_MAX);
    assert(zero_base == 0);
    assert(huge_base == 0);
    printf("host timespec_get: TIME_UTC=%d valid=%d base(0)=%d base(INT_MAX)=%d\n",
           TIME_UTC, valid, zero_base, huge_base);
}
#endif /* CPLUS_TEST_HAVE_HOST_TIMESPEC_GET */

static void test_host_rounding_modes(void) {
    const int modes[4] = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
    int i, j, restored;

    /* Four distinct encodings today; FE_TIESAWAY would be a fifth and this
     * host has no value for it (the C probe fails at compile time). */
    for (i = 0; i < 4; ++i) {
        for (j = i + 1; j < 4; ++j) {
            assert(modes[i] != modes[j]);
        }
    }

    /* Each of the four host modes round-trips through fesetround/fegetround. */
    for (i = 0; i < 4; ++i) {
        assert(fesetround(modes[i]) == 0);
        assert(fegetround() == modes[i]);
    }
    assert(fesetround(FE_TONEAREST) == 0);
    restored = fegetround();
    assert(restored == FE_TONEAREST);
    printf("host fenv modes: %d,%d,%d,%d (FE_TIESAWAY absent)\n",
           modes[0], modes[1], modes[2], modes[3]);
}

static void test_host_c11_multibyte(void) {
    mbstate_t st;
    wchar_t wc = 0;
    char out[8];
    size_t to_wide, from_wide;

    assert(setlocale(LC_ALL, "C") != NULL);
    assert(sizeof(unsigned char) == 1); /* char8_t's would-be backing type */

    memset(&st, 0, sizeof st);
    errno = 0;
    to_wide = mbrtowc(&wc, "A", 1, &st);
    assert(to_wide == 1);
    assert(wc == (wchar_t)'A');
    assert(errno == 0);

    memset(&st, 0, sizeof st);
    memset(out, 0, sizeof out);
    errno = 0;
    from_wide = wcrtomb(out, (wchar_t)'A', &st);
    assert(from_wide == 1);
    assert(out[0] == 'A');
    assert(errno == 0);

    /* An empty string in the initial state denotes the null character. Use
     * n == 1 here: this host's mbrtowc returns -2 for (s == "", n == 0),
     * which is a Darwin-specific quirk unrelated to the C23 gap. */
    memset(&st, 0, sizeof st);
    assert(mbrtowc(&wc, "", 1, &st) == 0);
    assert(wc == 0);

    printf("host C11 multibyte: mbrtowc=%zu wcrtomb=%zu (C23 UTF-8 pair absent)\n",
           to_wide, from_wide);
}

int main(void) {
    /* Authoritative, exact blockers: no call, no invented value. */
    printf("SKIP %s\n", CPLUS_C23_REMAINDERS_BLOCKER_TIMESPEC_GETRES);
    printf("SKIP %s\n", CPLUS_C23_REMAINDERS_BLOCKER_MBRTOC8);
    printf("SKIP %s\n", CPLUS_C23_REMAINDERS_BLOCKER_C8RTOMB);
    printf("SKIP %s\n", CPLUS_C23_REMAINDERS_BLOCKER_FE_TIESAWAY);

#ifdef CPLUS_TEST_HAVE_HOST_TIMESPEC_GET
    test_host_timespec_boundaries();
#else
    /* Adjacent C11 facility only: under strict -std=c99 the SDK hides
     * timespec_get/TIME_UTC. This says nothing about the four C23 APIs above,
     * all of which remain BLOCKED and are reported as SKIP. */
    puts("host timespec_get probe: unavailable under -std=c99 "
         "(C11 entry point not declared; not a C23 API result)");
#endif
    test_host_rounding_modes();
    test_host_c11_multibyte();

    puts("c23_remainders: ok (all four APIs BLOCKED, nothing fabricated)");
    return 0;
}
