#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "safe_alloc.h"
#include "safe_format.h"
#include "safe_io.h"
#include "safe_runtime.h"

/* Assert that fn(ctx) triggers a SafeCRT fail-stop exit (code 70). */
static void expect_trap(void (*fn)(void *), void *ctx) {
    pid_t pid;
    int status = 0;
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        fn(ctx);
        _exit(0);
    }
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 70);
}

static void child_cwe134_printf_user_input(void *unused) {
    char input[64];
    (void)unused;
    strcpy(input, "%x %x %x %s");
    /* Non-literal format string (CWE-134): must refuse before vprintf. */
    (void)printf_s(input);
}

static void child_cwe134_fprintf(void *unused) {
    char dyn[32];
    (void)unused;
    strcpy(dyn, "%s %x");
    (void)fprintf_s(stderr, dyn);
}

static void child_cwe134_snprintf(void *unused) {
    char buf[32];
    char dyn[32];
    (void)unused;
    strcpy(dyn, "%x %n");
    (void)snprintf_s(buf, sizeof(buf), dyn);
}

static void child_cwe134_sprintf(void *unused) {
    char buf[32];
    char dyn[32];
    (void)unused;
    strcpy(dyn, "%s");
    (void)sprintf_s(buf, sizeof(buf), dyn);
}

static void child_cwe134_sscanf(void *unused) {
    char dyn[16];
    int v = 0;
    (void)unused;
    strcpy(dyn, "%d");
    (void)sscanf_s("42", dyn, &v);
}

static void child_percent_n_printf(void *unused) {
    int count = 0;
    (void)unused;
    (void)printf_s("abc%n", &count);
}

static void child_percent_n_snprintf(void *unused) {
    char buf[32];
    long long n = 0;
    (void)unused;
    (void)snprintf_s(buf, sizeof(buf), "val=%1$lln", &n);
}

static void child_stack_dmax_overflow(void *unused) {
    char small[4];
    (void)unused;
    (void)sprintf_s(small, 64, "%s", "abcdefgh");
}

static void child_heap_dmax_overflow(void *unused) {
    char *hp = (char *)malloc_s(4);
    assert(hp != NULL);
    (void)unused;
    (void)snprintf_s(hp, 64, "%s", "abcdefgh");
    free_s(hp);
}

static void child_sscanf_unbounded_s(void *unused) {
    char buf[8];
    (void)unused;
    (void)sscanf_s("overflow", "%s", buf);
}

static void child_sscanf_unbounded_scanset(void *unused) {
    char buf[8];
    (void)unused;
    (void)sscanf_s("abcdef", "%[a-z]", buf);
}

static void child_uaf_dest(void *unused) {
    char *hp = (char *)malloc_s(16);
    assert(hp != NULL);
    (void)unused;
    free_s(hp);
    (void)snprintf_s(hp, 16, "%s", "hi");
}

static void child_uaf_fmt(void *unused) {
    char *hfmt = (char *)malloc_s(16);
    assert(hfmt != NULL);
    (void)unused;
    strcpy(hfmt, "%d");
    free_s(hfmt);
    (void)cplus_printf_s(hfmt, 1);
}

static void child_uaf_sscanf_src(void *unused) {
    char *hsrc = (char *)malloc_s(16);
    int v = 0;
    assert(hsrc != NULL);
    (void)unused;
    strcpy(hsrc, "42");
    free_s(hsrc);
    (void)sscanf_s(hsrc, "%d", &v);
}

int main(void) {
    char out[32];
    char word[16];
    int v = 0;
    FILE *f = NULL;

    /* 1. Default fail-stop violations (exit 70 before any dangerous libc call) */
    assert(cplus_runtime_get_violation_trap() == 1);
    expect_trap(child_cwe134_printf_user_input, NULL);
    expect_trap(child_cwe134_fprintf, NULL);
    expect_trap(child_cwe134_snprintf, NULL);
    expect_trap(child_cwe134_sprintf, NULL);
    expect_trap(child_cwe134_sscanf, NULL);
    expect_trap(child_percent_n_printf, NULL);
    expect_trap(child_percent_n_snprintf, NULL);
    expect_trap(child_stack_dmax_overflow, NULL);
    expect_trap(child_heap_dmax_overflow, NULL);
    expect_trap(child_sscanf_unbounded_s, NULL);
    expect_trap(child_sscanf_unbounded_scanset, NULL);
    expect_trap(child_uaf_dest, NULL);
    expect_trap(child_uaf_fmt, NULL);
    expect_trap(child_uaf_sscanf_src, NULL);

    /* 2. Safe literal format strings work even when data contains %x/%s/%n */
    {
        char payload[64] = "%x %x %x %s %n";
        assert(snprintf_s(out, sizeof(out), "%s", payload) == (int)strlen(payload));
        assert(strcmp(out, payload) == 0);
        assert(printf_s("%s\n", payload) > 0);
        assert(snprintf_s(out, sizeof(out), "100%%n %s", "ok") == 8);
        assert(strcmp(out, "100%n ok") == 0);
    }

    /* 3. Valid bounded scanf_s / fscanf_s / sscanf_s conversions */
    assert(sscanf_s("hello 42", "%5s %d", word, &v) == 2);
    assert(strcmp(word, "hello") == 0 && v == 42);
    v = 0;
    assert(sscanf_s("skip_me 99", "%*s %d", &v) == 1 && v == 99);
    memset(word, 0, sizeof(word));
    assert(sscanf_s("abc123", "%3[a-z]", word) == 1 && strcmp(word, "abc") == 0);
    f = fopen_s("/tmp/cplus_fmt_sec.tmp", "wb+");
    assert(f != NULL);
    assert(fprintf_s(f, "%s %d", "tok", 77) > 0);
    rewind(f);
    memset(word, 0, sizeof(word));
    v = 0;
    assert(fscanf_s(f, "%15s %d", word, &v) == 2 && strcmp(word, "tok") == 0 && v == 77);
    assert(fclose_s(f) == 0);
    unlink("/tmp/cplus_fmt_sec.tmp");

    /* 4. NULL stream/input checks return -1 and set EINVAL without trapping */
    errno = 0;
    assert(fprintf_s(NULL, "hi") == -1 && errno == EINVAL);
    errno = 0;
    assert(fscanf_s(NULL, "%d", &v) == -1 && errno == EINVAL);
    errno = 0;
    assert(sscanf_s(NULL, "%d", &v) == -1 && errno == EINVAL);
    errno = 0;
    assert(printf_s(NULL) == -1 && errno == EINVAL);
    errno = 0;
    assert(scanf_s(NULL) == -1 && errno == EINVAL);

    /* 5. Recoverable mode checks (-1 / EINVAL) with trap disabled */
    cplus_runtime_set_violation_trap(0);
    {
        char dyn[32] = "%x %x";
        char small[4];
        int n_sink = 0;
        char *hp = (char *)malloc_s(4);
        assert(hp != NULL);

        errno = 0;
        assert(printf_s(dyn) == -1 && errno == EINVAL);
        errno = 0;
        assert(fprintf_s(stderr, dyn) == -1 && errno == EINVAL);
        errno = 0;
        assert(snprintf_s(out, sizeof(out), dyn) == -1 && errno == EINVAL);
        errno = 0;
        assert(sprintf_s(out, sizeof(out), dyn) == -1 && errno == EINVAL);
        errno = 0;
        assert(scanf_s(dyn, &v) == -1 && errno == EINVAL);
        errno = 0;
        assert(fscanf_s(stdin, dyn, &v) == -1 && errno == EINVAL);
        errno = 0;
        assert(sscanf_s("12", dyn, &v) == -1 && errno == EINVAL);

        errno = 0;
        assert(cplus_printf_s("a%hhn", &n_sink) == -1 && errno == EINVAL);
        errno = 0;
        assert(cplus_fprintf_s(stderr, "%1$*2$.*3$ln", 1, 2, &n_sink) == -1 && errno == EINVAL);
        errno = 0;
        assert(sprintf_s(small, 32, "%s", "toolong") == -1 && errno == EINVAL);
        errno = 0;
        assert(snprintf_s(hp, 32, "%s", "toolong") == -1 && errno == EINVAL);
        errno = 0;
        assert(sscanf_s("abc", "%s", word) == -1 && errno == EINVAL);
        errno = 0;
        assert(sscanf_s("abc", "%[a-z]", word) == -1 && errno == EINVAL);
        errno = 0;
        assert(sscanf_s("abc", "%0s", word) == -1 && errno == EINVAL);
        errno = 0;
        assert(sscanf_s("abc", "%n", &n_sink) == -1 && errno == EINVAL);

        free_s(hp);
    }
    cplus_runtime_set_violation_trap(1);

    puts("unit format_security_s: ok");
    return 0;
}
