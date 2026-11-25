/* C+ side of the native safety-outcome matrix.
 *
 * Each scenario exercises a *runtime API contract* from the C+ checked layers
 * (stdlib/safe/safe_alloc.h, safe_memory.h, safe_error.h, safe_runtime.h).
 * Nothing here is a C+ compile-time check: every outcome is observed at run
 * time by calling a checked API and classifying what actually happened.
 *
 * Build (from the repo root, with the safe libs already built):
 *   PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
 *     build/c+ build --release benchmarks/safety_compare/safety_matrix.cp \
 *     -o <out> \
 *     -- -DCPLUS_EXIT_ARITHMETIC=110 -I. -Istdlib/safe -Ilibc \
 *        -Ilibc/install-include \
 *        build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
 *
 * `-DCPLUS_EXIT_ARITHMETIC=110` works around a transient mismatch in this
 * checkout: this repo's runtime/include header lacks the enum member that the
 * installed driver's c-plus-core/runtime/cplus_runtime.c uses when it rebuilds
 * the sanitizer-instrumented runtime. It does not change any scenario outcome
 * (verified identical with `--no-sanitize`).
 *
 * Modes:
 *   ./cplus_safety_matrix            -> "matrix": all recoverable scenarios,
 *                                       violation trap OFF, exit 0.
 *   ./cplus_safety_matrix matrix     -> same as default.
 *   ./cplus_safety_matrix fatal      -> ONE fatal-safety scenario (a checked
 *                                       UAF with the violation trap left ON).
 *                                       This mode MUST be run in a subprocess:
 *                                       it prints a diagnostic then _exit(70).
 *
 * Output protocol: one line per result, "SCENARIO <id> <OUTCOME> <detail>".
 * OUTCOME is OK | REFUSED | ABORT. The runner classifies these alongside the
 * Rust side; equality of tokens is evidence of observed behaviour only, not of
 * equivalent safety guarantees.
 */
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS 1
#include "stdlib/safe/safe_alloc.h"
#include "stdlib/safe/safe_memory.h"
#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_runtime.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void emit(const char *id, const char *outcome, const char *detail) {
    printf("SCENARIO %s %s %s\n", id, outcome, detail);
    fflush(stdout);
}

/* 1. null argument refusal ------------------------------------------------ */
static void s01_null_argument(void) {
    unsigned char src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    errno_t rc = cplus_memcpy_s(NULL, 8, src, 8);
    char detail[96];
    snprintf(detail, sizeof(detail), "cplus_memcpy_s(dest=NULL) rc=%d (ESNULLP=400)", (int)rc);
    emit("01_null_argument", rc == 400 ? "REFUSED" : "OK", detail);
}

/* 2. zero-length buffer --------------------------------------------------- */
static void s02_zero_length(void) {
    void *p = malloc_s(0); /* defined 1-byte tracked block */
    char detail[96];
    if (!p) {
        emit("02_zero_length", "REFUSED", "malloc_s(0) returned NULL");
        return;
    }
    snprintf(detail, sizeof(detail), "malloc_s(0) non-NULL tracked_size=%zu",
             cplus_safe_ptr_size(p));
    emit("02_zero_length", "OK", detail);
    free_s(p);
}

/* 3. exact-fit / one-over ------------------------------------------------- */
static void s03_exact_fit(void) {
    unsigned char dst[8] = {0};
    unsigned char src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    errno_t rc = cplus_memcpy_s(dst, 8, src, 8);
    char detail[96];
    snprintf(detail, sizeof(detail), "cplus_memcpy_s(dmax=8,smax=8) rc=%d dst[7]=%d",
             (int)rc, dst[7]);
    emit("03a_exact_fit", rc == 0 ? "OK" : "REFUSED", detail);
}

static void s03_one_over(void) {
    unsigned char dst[8] = {0};
    unsigned char src[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    errno_t rc = cplus_memcpy_s(dst, 8, src, 9);
    char detail[96];
    snprintf(detail, sizeof(detail), "cplus_memcpy_s(dmax=8,smax=9) rc=%d (ESNOSPC=406)",
             (int)rc);
    emit("03b_one_over", rc == 406 ? "REFUSED" : "OK", detail);
}

/* 4. integer overflow refusal -------------------------------------------- */
static void s04_integer_overflow(void) {
    void *p;
    char detail[96];
    errno = 0;
    p = calloc_s(SIZE_MAX, 2); /* product overflows RSIZE_MAX_MEM */
    /* EOVERFLOW is 84 on macOS arm64 (75 on Linux); print the observed value
     * rather than hard-coding a platform-specific number. */
    snprintf(detail, sizeof(detail), "calloc_s(SIZE_MAX,2)=%s errno=%d (EOVERFLOW=%d here)",
             p ? "non-NULL" : "NULL", errno, EOVERFLOW);
    emit("04_integer_overflow", (!p && errno == EOVERFLOW) ? "REFUSED" : "OK", detail);
    if (p) free_s(p);
}

/* 5. allocation failure atomicity ---------------------------------------- */
static void s05_alloc_atomicity(void) {
    unsigned char *p = (unsigned char *)malloc_s(32);
    void *q;
    int live, size_ok, byte_ok;
    char detail[128];
    if (!p) {
        emit("05_alloc_atomicity", "REFUSED", "malloc_s(32) returned NULL");
        return;
    }
    memset(p, 0x5a, 32);
    cplus_safe_alloc_fail_after(0); /* every subsequent alloc fails */
    q = realloc_s(p, 64);           /* must fail and leave p untouched */
    cplus_safe_alloc_fail_reset();
    live = cplus_safe_ptr_is_live(p);
    size_ok = (cplus_safe_ptr_size(p) == 32u);
    byte_ok = (p[0] == 0x5a && p[31] == 0x5a);
    snprintf(detail, sizeof(detail),
             "realloc_s=%s original_live=%d size32=%d bytes_intact=%d",
             q ? "non-NULL" : "NULL", live, size_ok, byte_ok);
    emit("05_alloc_atomicity",
         (!q && live && size_ok && byte_ok) ? "REFUSED" : "OK", detail);
    if (q) free_s(q); else free_s(p);
}

/* 6. use-after-drop refusal via metadata --------------------------------- */
static void s06_use_after_drop(void) {
    unsigned char *p = (unsigned char *)malloc_s(16);
    cplus_safe_alloc_meta m;
    unsigned char src[4] = {1, 2, 3, 4};
    int got_meta, was_freed, stale, rc;
    char detail[160];
    if (!p) {
        emit("06_use_after_drop", "REFUSED", "malloc_s(16) returned NULL");
        return;
    }
    got_meta = cplus_safe_alloc_get_meta(p, &m);
    free_s(p); /* "drop" the allocation */
    was_freed = cplus_safe_ptr_was_freed(p);
    stale = !cplus_safe_ptr_check_generation(p, m.generation);
    /* With the trap OFF this returns 400 and prints a UAF diagnostic; with the
     * trap ON (fatal mode) the same call terminates the process. */
    rc = (int)cplus_memcpy_s(p, 4, src, 4);
    snprintf(detail, sizeof(detail),
             "meta=%d was_freed=%d stale_gen=%d memcpy_rc=%d",
             got_meta, was_freed, stale, rc);
    emit("06_use_after_drop", (got_meta == 0 && was_freed && stale && rc == 400)
                                  ? "REFUSED" : "OK", detail);
}

/* 7. double-drop refusal -------------------------------------------------- */
static void s07_double_drop(void) {
    void *p = malloc_s(16);
    int rc1, rc2;
    char detail[96];
    if (!p) {
        emit("07_double_drop", "REFUSED", "malloc_s(16) returned NULL");
        return;
    }
    rc1 = free_checked_s(p); /* first release succeeds */
    rc2 = free_checked_s(p); /* second release refused */
    snprintf(detail, sizeof(detail), "free_checked_s rc=%d then rc=%d (EINVAL=22)", rc1, rc2);
    emit("07_double_drop", (rc1 == 0 && rc2 == CPLUS_SAFE_EINVAL) ? "REFUSED" : "OK", detail);
}

static void run_matrix(void) {
    /* Guarantee defined, non-aborting constraint returns for the recoverable
     * cases. The runtime UAF diagnostic is still emitted (trap OFF only means
     * "do not terminate"). */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_runtime_set_violation_trap(0);

    s01_null_argument();
    s02_zero_length();
    s03_exact_fit();
    s03_one_over();
    s04_integer_overflow();
    s05_alloc_atomicity();
    s06_use_after_drop();
    s07_double_drop();
    printf("MATRIX_DONE 8\n");
    fflush(stdout);
}

static void run_fatal(void) {
    /* Fatal-safety path: leave the violation trap ON (default). The checked API
     * detects a genuine use-after-free, prints a one-line diagnostic on stderr
     * and terminates with _exit(70). Only ever run this in a subprocess. */
    unsigned char *p = (unsigned char *)malloc_s(16);
    unsigned char src[4] = {1, 2, 3, 4};
    if (!p) {
        emit("fatal_uaf", "ABORT", "malloc_s(16) returned NULL (no violation exercised)");
        return;
    }
    free_s(p);
    emit("fatal_uaf", "ABORT", "about to call cplus_memcpy_s on freed block (trap ON)");
    (void)cplus_memcpy_s(p, 4, src, 4); /* prints diagnostic, then _exit(70) */
    /* Unreachable when the trap fires. */
    emit("fatal_uaf", "OK", "trap did not fire");
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "fatal") == 0) {
        run_fatal();
    } else {
        run_matrix();
    }
    return 0;
}
