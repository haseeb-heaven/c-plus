/*
 * tests/crt/unit/test_annex_k_closure.c
 *
 * Closure test for the Annex K stdio / stdlib audit recorded in
 * docs/libc/ANNEX_K_CLOSURE.md.
 *
 * SCOPE: this file exercises ONLY APIs that are already present and callable
 * from the checked C runtime today -- the cplus_* adapters declared in
 * stdlib/safe/safe_io.h and stdlib/safe/safe_format.h and exported by
 * build/safe/libcplus_safecrt.a.  Every call below resolves to a symbol that
 * was verified with `nm -g --defined-only build/safe/libcplus_safecrt.a`.
 *
 * OUT OF SCOPE (deliberately NOT referenced, because there is no cplus_*
 * export to call): gets_s, tmpnam_s, bsearch_s, qsort_s.  Their vendored
 * safeclib sources exist under third_party/safeclib/src but are not compiled
 * into the archive and have no stdlib/safe header declaration.  See the audit
 * document for their exact interfaces and additive-wrapper proposals.  No
 * fabricated wrappers or local stand-ins are defined here.
 *
 * Build (from repository root):
 *   /usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
 *     -I. -Istdlib/safe -Ithird_party/safeclib/include \
 *     tests/crt/unit/test_annex_k_closure.c \
 *     build/safe/libcplus_safecrt.a -lm -pthread -o /tmp/test_annex_k_closure
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "safe_error.h"
#include "safe_format.h"
#include "safe_io.h"

#define IO_PATH "/tmp/cplus_annex_k_closure_io.tmp"
#define SCAN_PATH "/tmp/cplus_annex_k_closure_scan.tmp"
#define STDIN_PATH "/tmp/cplus_annex_k_closure_stdin.tmp"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                           \
    do {                                                                      \
        ++checks;                                                             \
        if (!(cond)) {                                                        \
            ++failures;                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

/* Write a tiny host-C fixture; fixture setup is not the API under test. */
static int write_fixture(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    if (fputs(text, f) == EOF) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}

/* (b) fopen_s / fclose_s / fread_s / fprintf_s / fscanf_s share one stream. */
static void test_io_family(void) {
    FILE *f = cplus_fopen_s(IO_PATH, "wb+");
    CHECK(f != NULL);
    if (f) {
        CHECK(cplus_fprintf_s(f, "hello %d", 42) >= 0);
        CHECK(fflush(f) == 0);
        CHECK(fseek(f, 0, SEEK_SET) == 0);

        {
            char buf[64];
            size_t n;
            memset(buf, 0, sizeof(buf));
            n = cplus_fread_s(buf, sizeof(buf) - 1, 1, sizeof(buf) - 1, f);
            CHECK(n == strlen("hello 42"));
            CHECK(strcmp(buf, "hello 42") == 0);
        }

        CHECK(cplus_fclose_s(f) == 0);
    }

    errno = 0;
    CHECK(cplus_fopen_s(NULL, "rb") == NULL);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(cplus_fclose_s(NULL) == EOF);
    CHECK(errno == EINVAL);

    unlink(IO_PATH);
}

/* (b) fscanf_s on a numeric fixture. */
static void test_fscanf(void) {
    int a = 0, b = 0;
    FILE *f;
    CHECK(write_fixture(SCAN_PATH, "42 7\n") == 0);
    f = cplus_fopen_s(SCAN_PATH, "rb");
    CHECK(f != NULL);
    if (f) {
        CHECK(cplus_fscanf_s(f, "%d %d", &a, &b) == 2);
        CHECK(a == 42);
        CHECK(b == 7);
        CHECK(cplus_fclose_s(f) == 0);
    }
    errno = 0;
    CHECK(cplus_fscanf_s(NULL, "%d", &a) == -1);
    CHECK(errno == EINVAL);
    unlink(SCAN_PATH);
}

/* (b) printf_s -- observable via its return contract. */
static void test_printf(void) {
    CHECK(cplus_printf_s("annex-k closure: printf_s ok\n") >= 0);
    errno = 0;
    CHECK(cplus_printf_s(NULL) == -1);
    CHECK(errno == EINVAL);
}

/* (b) sprintf_s -- bounded destination via dmax. */
static void test_sprintf(void) {
    char buf[64];
    memset(buf, 0, sizeof(buf));
    CHECK(cplus_sprintf_s(buf, sizeof(buf), "v=%d s=%s", 42, "ok") >= 0);
    CHECK(strcmp(buf, "v=42 s=ok") == 0);
    errno = 0;
    CHECK(cplus_sprintf_s(buf, sizeof(buf), NULL) == -1);
    CHECK(errno == EINVAL);
}

/* (b) sscanf_s on an in-memory string. */
static void test_sscanf(void) {
    int a = 0, b = 0;
    CHECK(cplus_sscanf_s("42 7", "%d %d", &a, &b) == 2);
    CHECK(a == 42);
    CHECK(b == 7);
    errno = 0;
    CHECK(cplus_sscanf_s(NULL, "%d", &a) == -1);
    CHECK(errno == EINVAL);
}

/* (b) scanf_s -- feed stdin from a fixture using dup/dup2, then restore it. */
static void test_scanf(void) {
    int saved_stdin = -1;
    int fd = -1;
    int v = 0;
    CHECK(write_fixture(STDIN_PATH, "123\n") == 0);
    saved_stdin = dup(STDIN_FILENO);
    CHECK(saved_stdin >= 0);
    fd = open(STDIN_PATH, O_RDONLY);
    CHECK(fd >= 0);
    if (saved_stdin >= 0 && fd >= 0) {
        CHECK(dup2(fd, STDIN_FILENO) == STDIN_FILENO);
        CHECK(cplus_scanf_s("%d", &v) == 1);
        CHECK(v == 123);
        CHECK(dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO);
    }
    if (fd >= 0)
        close(fd);
    if (saved_stdin >= 0)
        close(saved_stdin);
    errno = 0;
    CHECK(cplus_scanf_s(NULL) == -1);
    CHECK(errno == EINVAL);
    unlink(STDIN_PATH);
}

int main(void) {
    test_io_family();
    test_fscanf();
    test_printf();
    test_sprintf();
    test_sscanf();
    test_scanf();

    if (failures == 0) {
        printf("annex_k_closure: %d/%d checks passed; 9/9 cplus_* APIs "
               "callable\n", checks, checks);
        return 0;
    }
    fprintf(stderr, "annex_k_closure: %d/%d checks FAILED\n", failures, checks);
    return 1;
}
