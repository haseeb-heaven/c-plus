/* BLUE-TEAM agent 9: defense-in-depth regressions for the checked format API.
 *
 * A "REFUSED" outcome means the adapter detected the hostile input and returned
 * its defined error (-1 / EINVAL) or, with the trap enabled, exited 70 before
 * any host formatter was reached.  Valid-input semantics are checked as well so
 * the hardening is demonstrably additive.
 *
 * Build/run instructions are in HARDENING.md next to this directory.
 */
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_format.h"
#include "safe_io.h"
#include "safe_log.h"
#include "safe_runtime.h"

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

/* ---------------------------------------------------------------------------
 * 1. %n rejection (all length-modifier/positional forms) in both printf and
 *    scanf families.  Run with the trap enabled to prove termination happens
 *    before any %n write.
 * ------------------------------------------------------------------------- */
static void child_percent_n_printf(void *unused) {
    int sink = 0;
    (void)unused;
    (void)cplus_printf_s("val%n", &sink);
}
static void child_percent_n_fprintf(void *unused) {
    int sink = 0;
    (void)unused;
    (void)cplus_fprintf_s(stderr, "%1$*2$lln", 1, 2, &sink);
}
static void child_percent_n_snprintf(void *unused) {
    char buf[32];
    long long sink = 0;
    (void)unused;
    (void)cplus_snprintf_s(buf, sizeof buf, "val=%1$lln", &sink);
}
static void child_percent_n_sprintf(void *unused) {
    char buf[32];
    (void)unused;
    (void)cplus_sprintf_s(buf, sizeof buf, "%hhn", (int *)0);
}
static void child_percent_n_sscanf(void *unused) {
    int sink = 0;
    (void)unused;
    (void)cplus_sscanf_s("42", "%d%n", (int *)&sink, &sink);
}

static void expect_trap(void (*fn)(void *), void *ctx) {
    pid_t pid = fork();
    int status = 0;
    CHECK(pid >= 0);
    if (pid == 0) { fn(ctx); _exit(0); }
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 70);
}

static void test_percent_n(void) {
    expect_trap(child_percent_n_printf, NULL);
    expect_trap(child_percent_n_fprintf, NULL);
    expect_trap(child_percent_n_snprintf, NULL);
    expect_trap(child_percent_n_sprintf, NULL);
    expect_trap(child_percent_n_sscanf, NULL);
}

/* ---------------------------------------------------------------------------
 * 2. Non-literal format strings are refused by the unprefixed macros.
 * ------------------------------------------------------------------------- */
static void child_nonliteral_printf(void *unused) {
    char fmt[32];
    (void)unused;
    strcpy(fmt, "%x %x %x %s");
    (void)printf_s(fmt);
}
static void child_nonliteral_snprintf(void *unused) {
    char fmt[32], buf[32];
    (void)unused;
    strcpy(fmt, "%x %n");
    (void)snprintf_s(buf, sizeof buf, fmt);
}
static void child_nonliteral_sscanf(void *unused) {
    char fmt[16];
    int v = 0;
    (void)unused;
    strcpy(fmt, "%d");
    (void)sscanf_s("42", fmt, &v);
}

static void test_nonliteral(void) {
    expect_trap(child_nonliteral_printf, NULL);
    expect_trap(child_nonliteral_snprintf, NULL);
    expect_trap(child_nonliteral_sscanf, NULL);
}

/* ---------------------------------------------------------------------------
 * 3. One-byte-too-small output buffer (declared dmax exceeds the real object)
 *    and NUL termination / guard bytes on plain truncation.
 * ------------------------------------------------------------------------- */
static void child_stack_dmax_one_too_big(void *unused) {
    char small[8];
    (void)unused;
    (void)sprintf_s(small, sizeof(small) + 1, "%s", "abcdefghij");
}
static void child_heap_dmax_one_too_big(void *unused) {
    char *hp = (char *)malloc_s(8);
    (void)unused;
    CHECK(hp != NULL);
    (void)snprintf_s(hp, 9, "%s", "abcdefghij");
    free_s(hp);
}

static void test_dmax_and_termination(void) {
    char buf[16];
    expect_trap(child_stack_dmax_one_too_big, NULL);
    expect_trap(child_heap_dmax_one_too_big, NULL);

    /* Valid truncation: never writes past dmax, always NUL-terminates, returns
     * the would-be length.  The byte at index dmax is untouched. */
    memset(buf, 'Z', sizeof buf);
    CHECK(cplus_snprintf_s(buf, 4, "%s", "abcdefgh") == 8);
    CHECK(strlen(buf) == 3);
    CHECK(buf[3] == '\0');
    CHECK(buf[4] == 'Z');
    /* exact fit and dmax==1 */
    memset(buf, 'Z', sizeof buf);
    CHECK(cplus_snprintf_s(buf, 4, "abc") == 3);
    CHECK(strcmp(buf, "abc") == 0);
    memset(buf, 'Z', sizeof buf);
    CHECK(cplus_snprintf_s(buf, 1, "abc") == 3);
    CHECK(buf[0] == '\0' && buf[1] == 'Z');
    /* NULL dest is legal only with dmax == 0 ("query" form) */
    CHECK(cplus_snprintf_s(NULL, 8, "%s", "x") == -1);
    CHECK(cplus_snprintf_s(NULL, 0, "%s", "x") == 1);
}

/* ---------------------------------------------------------------------------
 * 4. Null dest/format/stream refuse without trapping and set EINVAL.
 * ------------------------------------------------------------------------- */
static void test_null_args(void) {
    char buf[16];
    int v = 0;
    CHECK(cplus_runtime_get_violation_trap() == 1);
    errno = 0;
    CHECK(cplus_snprintf_s(buf, sizeof buf, NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_printf_s(NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_fprintf_s(NULL, "hi") == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_fscanf_s(NULL, "%d", &v) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_sscanf_s(NULL, "%d", &v) == -1 && errno == EINVAL);
}

/* ---------------------------------------------------------------------------
 * 5. Huge dmax on a tracked buffer is clamped/refused.  SIZE_MAX here also
 *    exercises integer-overflow paths in capacity arithmetic.
 * ------------------------------------------------------------------------- */
static void child_huge_dmax(void *unused) {
    char small[8];
    (void)unused;
    (void)snprintf_s(small, (size_t)-1, "%s", "abcdefghij");
}
static void test_huge_dmax(void) {
    char buf[8];
    expect_trap(child_huge_dmax, NULL);
    /* Trap disabled: the macro (which carries the compile-time object size)
     * returns -1 / EINVAL for the same huge dmax. */
    cplus_runtime_set_violation_trap(0);
    errno = 0;
    CHECK(snprintf_s(buf, (size_t)-1, "%s", "abc") == -1);
    CHECK(errno == EINVAL);
    cplus_runtime_set_violation_trap(1);
}

/* ---------------------------------------------------------------------------
 * 6. Constraint-mode bypass: format/scanf adapters must enforce their checks
 *    independently of the safeclib constraint handler mode (IGNORE vs ABORT).
 * ------------------------------------------------------------------------- */
static void test_constraint_mode(void) {
    char fmt[16] = "%x %x";
    char buf[8];
    cplus_safe_constraint_mode old = cplus_safe_get_constraint_mode();
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_runtime_set_violation_trap(0);
    errno = 0;
    CHECK(sprintf_s(buf, sizeof buf, fmt) == -1 && errno == EINVAL);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_ABORT);
    errno = 0;
    CHECK(sprintf_s(buf, sizeof buf, fmt) == -1 && errno == EINVAL);
    cplus_safe_set_constraint_mode(old);
    cplus_runtime_set_violation_trap(1);
}

/* ---------------------------------------------------------------------------
 * 7. NEW GAP: use-after-close stream.  Previously fprintf_s/fscanf_s only
 *    checked stream != NULL; a FILE* closed through cplus_fclose_s was passed
 *    straight to vfprintf/vfscanf (a UAF).  Now it is REFUSED.
 * ------------------------------------------------------------------------- */
static void child_fprintf_closed(void *unused) {
    FILE *f = cplus_fopen_s("/tmp/cplus_def_fmt_closed.tmp", "wb+");
    (void)unused;
    CHECK(f != NULL);
    CHECK(cplus_fclose_s(f) == 0);
    (void)cplus_fprintf_s(f, "%s", "after close");
}
static void child_fscanf_closed(void *unused) {
    FILE *f = cplus_fopen_s("/tmp/cplus_def_fmt_closed.tmp", "wb+");
    int v = 0;
    (void)unused;
    CHECK(f != NULL);
    CHECK(cplus_fclose_s(f) == 0);
    (void)cplus_fscanf_s(f, "%d", &v);
}

static void test_closed_stream(void) {
    expect_trap(child_fprintf_closed, NULL);
    expect_trap(child_fscanf_closed, NULL);

    /* Recoverable mode: -1 / EINVAL, no host I/O. */
    cplus_runtime_set_violation_trap(0);
    {
        FILE *f = cplus_fopen_s("/tmp/cplus_def_fmt_closed.tmp", "wb+");
        int v = 0;
        CHECK(f != NULL && cplus_fclose_s(f) == 0);
        errno = 0;
        CHECK(cplus_fprintf_s(f, "%s", "after close") == -1 && errno == EINVAL);
        errno = 0;
        CHECK(cplus_fscanf_s(f, "%d", &v) == -1 && errno == EINVAL);
        /* Still closable once after the first close? No: double close refused. */
        errno = 0;
        CHECK(cplus_fclose_s(f) == EOF && errno == EINVAL);
    }
    cplus_runtime_set_violation_trap(1);
    unlink("/tmp/cplus_def_fmt_closed.tmp");
}

/* ---------------------------------------------------------------------------
 * 8. Valid-input semantics unchanged: literal formats, bounded scans with
 *    widths, and normal stream I/O all still work.
 * ------------------------------------------------------------------------- */
static void test_valid_semantics(void) {
    char out[32], word[16];
    int v = 0;
    FILE *f;

    CHECK(cplus_snprintf_s(out, sizeof out, "%s", "100%%n ok") == 9);
    CHECK(strcmp(out, "100%%n ok") == 0); /* %n in DATA is fine, not a specifier */
    CHECK(cplus_snprintf_s(out, sizeof out, "100%%%%n ok") == 9);
    CHECK(strcmp(out, "100%%n ok") == 0); /* literal %% in the format is fine */
    CHECK(cplus_printf_s("%s\n", "literal format") > 0);

    CHECK(cplus_sscanf_s("hello 42", "%5s %d", word, &v) == 2);
    CHECK(strcmp(word, "hello") == 0 && v == 42);
    CHECK(cplus_sscanf_s("skip 99", "%*s %d", &v) == 1 && v == 99);

    f = cplus_fopen_s("/tmp/cplus_def_fmt_valid.tmp", "wb+");
    CHECK(f != NULL);
    CHECK(cplus_fprintf_s(f, "%s %d", "tok", 77) > 0);
    rewind(f);
    memset(word, 0, sizeof word);
    v = 0;
    CHECK(cplus_fscanf_s(f, "%15s %d", word, &v) == 2);
    CHECK(strcmp(word, "tok") == 0 && v == 77);
    CHECK(cplus_fclose_s(f) == 0);
    unlink("/tmp/cplus_def_fmt_valid.tmp");
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    CHECK(cplus_runtime_get_violation_trap() == 1);

    test_percent_n();
    test_nonliteral();
    test_dmax_and_termination();
    test_null_args();
    test_huge_dmax();
    test_constraint_mode();
    test_closed_stream();
    test_valid_semantics();

    if (failures == 0) {
        puts("test_format_hardening: ok");
        return 0;
    }
    printf("test_format_hardening: FAILURES=%d\n", failures);
    return 1;
}
