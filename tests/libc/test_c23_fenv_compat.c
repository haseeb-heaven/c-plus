/*
 * test_c23_fenv_compat.c -- honest status test for C23 FE_TIESAWAY (<fenv.h>).
 *
 * The host compiler/SDK does not define FE_TIESAWAY. This test does not fake
 * it. It checks the capability query from cplus_c23_fenv_compat.h, re-runs the
 * exact compile probe at runtime (when a compiler is available), verifies the
 * surrounding host <fenv.h> still works, and enforces a 30-second budget.
 *
 * Every check is counted; nothing aborts early. The final line is exactly:
 *   c23_fenv: <checks> <failures>
 * Exit status is nonzero iff failures > 0.
 *
 * Build/run (standalone; no cplus_* symbol is referenced):
 *   /usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
 *     -I../../libc/install-include test_c23_fenv_compat.c \
 *     -o /tmp/test_c23_fenv_compat
 *   /tmp/test_c23_fenv_compat
 *
 * The standard libc runner builds every test with
 *   -O2 -Wall -Wextra -pedantic -std=c99 -Ilibc -Ilibc/include
 *   -Ilibc/install-include <test> <libcplus_hardened.a> -lm -pthread
 * This file builds unchanged under that command.
 *
 * The runtime re-probe uses CPLUS_C23_FENV_CLANG when set (e.g. a different
 * clang), otherwise /usr/bin/clang -- the exact compiler used to record the
 * evidence in docs/libc/C23_FENV_COMPAT.md.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif

#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../libc/install-include/cplus_c23_fenv_compat.h"

#ifndef CPLUS_C23_FENV_CLANG
#define CPLUS_C23_FENV_CLANG "/usr/bin/clang"
#endif

/* Recorded probe evidence, verbatim (2026-11-23, /usr/bin/clang, -std=c11).
 * The shell form contains literal backslash-n sequences so that POSIX
 * printf(1) turns them into the two source lines used in the original probe. */
#define CPLUS_C23_FENV_PROBE_SOURCE_SHELL                                      \
    "#include <fenv.h>\\nint main(void) { return FE_TIESAWAY; }\\n"
#define CPLUS_C23_FENV_PROBE_DIAGNOSTIC                                        \
    "<stdin>:2:25: error: use of undeclared identifier 'FE_TIESAWAY'"

static int checks;
static int failures;

static void check(int condition, const char *label) {
    ++checks;
    if (condition) {
        printf("PASS %s\n", label);
    } else {
        ++failures;
        printf("FAIL %s\n", label);
    }
}

/* Compile-time guards: the header must not synthesize the macro on a host
 * that lacks it, and the capability value must be a strict boolean. */
#if !CPLUS_C23_FENV_HAVE_FE_TIESAWAY
#ifdef FE_TIESAWAY
#error "compiler defines FE_TIESAWAY but compat header reports it unavailable"
#endif
#ifdef CPLUS_FE_TIESAWAY
#error "compat header must not synthesize CPLUS_FE_TIESAWAY on this host"
#endif
#endif
typedef char cplus_fenv_compat_capability_is_boolean[
    (CPLUS_C23_FENV_HAVE_FE_TIESAWAY == 0 ||
     CPLUS_C23_FENV_HAVE_FE_TIESAWAY == 1) ? 1 : -1];

/* Positive control: the host fenv works; it is the C23 macro that is absent. */
static void check_host_fenv(void) {
    const int modes[4] = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
    int i, j, distinct, roundtrip;

    distinct = 1;
    for (i = 0; i < 4; ++i) {
        for (j = i + 1; j < 4; ++j) {
            if (modes[i] == modes[j]) {
                distinct = 0;
            }
        }
    }
    check(distinct, "host exposes four distinct rounding modes");

    roundtrip = 1;
    for (i = 0; i < 4; ++i) {
        if (fesetround(modes[i]) != 0 || fegetround() != modes[i]) {
            roundtrip = 0;
        }
    }
    (void)fesetround(FE_TONEAREST);
    check(roundtrip, "host modes round-trip via fesetround/fegetround");

#if CPLUS_C23_FENV_HAVE_FE_TIESAWAY
    printf("host fenv evidence: modes %d,%d,%d,%d plus FE_TIESAWAY=%d\n",
           modes[0], modes[1], modes[2], modes[3], FE_TIESAWAY);
#else
    printf("host fenv evidence: modes %d,%d,%d,%d (FE_TIESAWAY absent)\n",
           modes[0], modes[1], modes[2], modes[3]);
#endif
}

/* Re-run the exact recorded probe live. Output is combined stderr+stdout so
 * the compiler diagnostic is captured verbatim. A missing compiler is a SKIP,
 * not a failure: the capability query result is the authoritative check. */
static void reprobe(void) {
    const char *compiler = getenv("CPLUS_C23_FENV_CLANG");
    char command[512];
    char output[1024];
    FILE *pipe;
    size_t got;
    int status;

    if (compiler == NULL || compiler[0] == '\0') {
        compiler = CPLUS_C23_FENV_CLANG;
    }
    (void)snprintf(command, sizeof command,
                   "printf '%s' | %s -x c -std=c11 -fsyntax-only - 2>&1",
                   CPLUS_C23_FENV_PROBE_SOURCE_SHELL, compiler);

    printf("recorded probe command: printf '#include <fenv.h>\\n"
           "int main(void) { return FE_TIESAWAY; }\\n' | %s "
           "-x c -std=c11 -fsyntax-only - 2>&1\n", compiler);
    printf("recorded probe output (verbatim):\n%s\n1 error generated.\n",
           CPLUS_C23_FENV_PROBE_DIAGNOSTIC);

    pipe = popen(command, "r");
    if (pipe == NULL) {
        puts("SKIP live re-probe: popen() failed (compiler unavailable)");
        return;
    }
    got = fread(output, 1, sizeof output - 1, pipe);
    output[got] = '\0';
    status = pclose(pipe);

    printf("live probe command: %s\n", command);
    printf("live probe output (verbatim):\n%s", output);

#if CPLUS_C23_FENV_HAVE_FE_TIESAWAY
    check(strstr(output, "error:") == NULL,
          "future host: probe compiles FE_TIESAWAY successfully");
    check(status == 0, "future host: probe exits zero");
    check(CPLUS_FE_TIESAWAY == FE_TIESAWAY,
          "future host: wrapper aliases the real FE_TIESAWAY macro");
#else
    check(strstr(output, CPLUS_C23_FENV_PROBE_DIAGNOSTIC) != NULL,
          "live probe reports: use of undeclared identifier 'FE_TIESAWAY'");
    check(status != 0, "live probe exits nonzero (compile failure)");
    check(cplus_fenv_has_tiesaway() == 0,
          "cplus_fenv_has_tiesaway() reports 0 (false)");
#endif
}

int main(void) {
    time_t started = time(NULL);
    double elapsed;

    printf("classification: %s\n", CPLUS_C23_FENV_TIESAWAY_STATUS);
    printf("blocker: %s\n", CPLUS_C23_FENV_BLOCKER_FE_TIESAWAY);

#if CPLUS_C23_FENV_HAVE_FE_TIESAWAY
    check(strcmp(CPLUS_C23_FENV_TIESAWAY_STATUS, "SUPPORTED-BY-HOST") == 0,
          "status macro is SUPPORTED-BY-HOST");
#else
    check(strcmp(CPLUS_C23_FENV_TIESAWAY_STATUS, "UNSUPPORTED-BY-HOST") == 0,
          "status macro is UNSUPPORTED-BY-HOST");
    check(strstr(CPLUS_C23_FENV_BLOCKER_FE_TIESAWAY,
                 "use of undeclared identifier 'FE_TIESAWAY'") != NULL,
          "blocker carries the exact probe diagnostic");
#endif

    reprobe();
    check_host_fenv();

    elapsed = difftime(time(NULL), started);
    check(elapsed < 30.0, "completed within the 30-second budget");

    printf("c23_fenv: %d %d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
