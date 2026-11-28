/*
 * test_violation_log.c -- standalone C99 proof of the additive, bounded
 * runtime-violation diagnostic log (libc/src/runtime/violation_log.c).
 *
 * Build (the new source is compiled alongside the hardened libc archive):
 *   /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -pthread \
 *     -Ilibc/install-include \
 *     tests/libc/test_violation_log.c libc/src/runtime/violation_log.c \
 *     build/libc/libcplus_hardened.a -lm -o /tmp/test_violation_log
 *
 * What is PROVEN below:
 *   (1)  close() on a closed log is a safe no-op; initial count is 0.
 *   (2)  event() refuses when the log is not open (EBADF); count unchanged.
 *   (3)  open(NULL) / open("") refuse with EINVAL.
 *   (4)  event() refuses a NULL/empty `kind` with EINVAL; count unchanged.
 *   (5)  Known events log with the exact fixed-format content (code, kind,
 *        hex pointer, size), and the count tracks successful events.
 *   (6)  The file is append-only across close()/re-open.
 *   (7)  Canary guards around a long `kind` buffer stay intact; every line is
 *        bounded in length (no overflow / no unbounded formatting).
 *   (8)  Concurrency: 2 threads x 100 events = 200, no lost or interleaved
 *        lines, all 200 present with correct prefixes.
 *   (9)  A failed open returns a nonzero errno and does not change the count.
 *
 * Output: "violation_log: <checks> <failures>"; exit status is nonzero when
 * any check fails.  Path arguments are optional (machine-independent defaults
 * are relative to the working directory).
 */

#include "cplus_violation_log.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;
static volatile int g_thread_errors = 0;

#define CHECK(cond, desc)                                                      \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            printf("FAIL: %s\n", (desc));                                      \
        }                                                                      \
    } while (0)

/* Read a whole file into a NUL-terminated buffer; return byte count. */
static size_t read_file(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f)
        return (size_t)-1;
    n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    fclose(f);
    return n;
}

static size_t count_lines(const char *buf) {
    size_t c = 0;
    while (*buf) {
        if (*buf == '\n')
            ++c;
        ++buf;
    }
    return c;
}

/* Longest newline-delimited line in the buffer (excluding the newline). */
static size_t max_line_len(const char *buf) {
    size_t best = 0, cur = 0;
    while (*buf) {
        if (*buf == '\n') {
            if (cur > best)
                best = cur;
            cur = 0;
        } else {
            ++cur;
        }
        ++buf;
    }
    if (cur > best)
        best = cur;
    return best;
}

static size_t count_prefix(const char *buf, const char *prefix) {
    size_t c = 0;
    const char *p = buf;
    size_t lp = strlen(prefix);
    while (*p) {
        if (strncmp(p, prefix, lp) == 0)
            ++c;
        while (*p && *p != '\n')
            ++p;
        if (*p == '\n')
            ++p;
    }
    return c;
}

/* A concurrency line must be exactly "<code> <kind> 0x<hex> <size>\n". */
static int valid_concurrency_lines(const char *buf) {
    char line[512];
    const char *p = buf;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len, f;
        int code, fields;
        char kind[32];
        unsigned long long pv;
        unsigned long sz;
        char tail;
        if (!e)
            return 0;
        len = (size_t)(e - p);
        if (len == 0 || len >= sizeof(line))
            return 0;
        memcpy(line, p, len);
        line[len] = '\0';
        fields = sscanf(line, "%d %31s 0x%llx %lu%c", &code, kind, &pv, &sz,
                        &tail);
        f = (size_t)fields;
        if (f != 4)
            return 0;
        p = e + 1;
    }
    return 1;
}

typedef struct {
    int code;
    const char *kind;
    int iterations;
} thread_arg;

static void *thread_fn(void *opaque) {
    thread_arg *a = (thread_arg *)opaque;
    int i;
    for (i = 0; i < a->iterations; ++i) {
        void *p = (void *)(uintptr_t)(0x1000UL + (unsigned long)i);
        if (cplus_violation_log_event(a->code, a->kind, p,
                                      (size_t)i) != 0) {
            __sync_fetch_and_add(&g_thread_errors, 1);
        }
    }
    return NULL;
}

/* Canary-guarded long kind buffer: guards must survive an event() call. */
struct guarded {
    unsigned char pre[16];
    char kind[160];
    unsigned char post[16];
};

static int guards_intact(const struct guarded *g) {
    size_t i;
    for (i = 0; i < sizeof(g->pre); ++i)
        if (g->pre[i] != 0xA5u)
            return 0;
    for (i = 0; i < sizeof(g->post); ++i)
        if (g->post[i] != 0x5Au)
            return 0;
    return 1;
}

int main(int argc, char **argv) {
    const char *path_a = (argc > 1) ? argv[1] : "violation_log_a.log";
    const char *path_b = (argc > 2) ? argv[2] : "violation_log_b.log";
    char buf[32768];
    size_t n;
    unsigned long base;
    pthread_t t1, t2;
    thread_arg a1, a2;
    struct guarded g;

    remove(path_a);
    remove(path_b);

    /* (1) closed-state behaviour */
    cplus_violation_log_close();
    CHECK(cplus_violation_log_count() == 0UL, "initial count is 0");
    cplus_violation_log_close(); /* safe no-op */

    /* (2) refuse when not open */
    CHECK(cplus_violation_log_event(1, "x", NULL, 0) == EBADF,
          "event before open returns EBADF");
    CHECK(cplus_violation_log_count() == 0UL, "refused event not counted");

    /* (3) invalid open arguments */
    CHECK(cplus_violation_log_open(NULL) == EINVAL, "open(NULL) == EINVAL");
    CHECK(cplus_violation_log_open("") == EINVAL, "open(\"\") == EINVAL");

    /* (4) invalid event arguments (log now open) */
    CHECK(cplus_violation_log_open(path_a) == 0, "open path A succeeds");
    CHECK(cplus_violation_log_event(1, NULL, NULL, 0) == EINVAL,
          "event(NULL kind) == EINVAL");
    CHECK(cplus_violation_log_event(1, "", NULL, 0) == EINVAL,
          "event(empty kind) == EINVAL");
    CHECK(cplus_violation_log_count() == 0UL,
          "invalid-kind events not counted");

    /* (5) known events and exact content */
    CHECK(cplus_violation_log_event(101, "bounds", (void *)(uintptr_t)0x1234UL,
                                    42) == 0,
          "log bounds event");
    CHECK(cplus_violation_log_event(102, "nonnull", (void *)(uintptr_t)0xabcdUL,
                                    7) == 0,
          "log nonnull event");
    CHECK(cplus_violation_log_event(103, "double-free", NULL, 0) == 0,
          "log double-free event");
    CHECK(cplus_violation_log_count() == 3UL, "count == 3 after 3 events");

    n = read_file(path_a, buf, sizeof(buf));
    CHECK(n != (size_t)-1, "log A readable");
    CHECK(count_lines(buf) == 3, "log A has exactly 3 lines");
    CHECK(strstr(buf, "101 bounds 0x1234 42\n") != NULL,
          "bounds line content exact");
    CHECK(strstr(buf, "102 nonnull 0xabcd 7\n") != NULL,
          "nonnull line content exact");
    CHECK(strstr(buf, "103 double-free 0x0 0\n") != NULL,
          "double-free line content exact");

    /* (7) canary guards + bounded lines */
    memset(&g, 0, sizeof(g));
    memset(g.pre, 0xA5, sizeof(g.pre));
    memset(g.post, 0x5A, sizeof(g.post));
    memset(g.kind, 'K', sizeof(g.kind) - 1);
    g.kind[sizeof(g.kind) - 1] = '\0';
    CHECK(cplus_violation_log_event(105, g.kind, (void *)(uintptr_t)0x55UL,
                                    9) == 0,
          "log long-kind event");
    CHECK(guards_intact(&g), "canary guards intact after event");
    CHECK(cplus_violation_log_count() == 4UL, "count == 4 after long-kind event");
    n = read_file(path_a, buf, sizeof(buf));
    CHECK(count_lines(buf) == 4, "log A has 4 lines after long-kind event");
    CHECK(max_line_len(buf) < 256, "every line is bounded (< 256 chars)");

    /* (6) close + re-open appends rather than truncates */
    cplus_violation_log_close();
    CHECK(cplus_violation_log_open(path_a) == 0, "re-open path A succeeds");
    CHECK(cplus_violation_log_event(104, "reopen", (void *)(uintptr_t)0x1UL,
                                    1) == 0,
          "log event after re-open");
    CHECK(cplus_violation_log_count() == 5UL, "count == 5 (cumulative)");
    n = read_file(path_a, buf, sizeof(buf));
    CHECK(count_lines(buf) == 5, "re-open appended, earlier lines preserved");

    /* (8) concurrency: 2 threads x 100 events */
    cplus_violation_log_close();
    CHECK(cplus_violation_log_open(path_b) == 0, "open path B for concurrency");
    base = cplus_violation_log_count();
    a1.code = 201;
    a1.kind = "alpha";
    a1.iterations = 100;
    a2.code = 202;
    a2.kind = "beta";
    a2.iterations = 100;
    CHECK(pthread_create(&t1, NULL, thread_fn, &a1) == 0, "thread 1 created");
    CHECK(pthread_create(&t2, NULL, thread_fn, &a2) == 0, "thread 2 created");
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    CHECK(g_thread_errors == 0, "no refused events in worker threads");
    CHECK(cplus_violation_log_count() == base + 200UL,
          "count increased by exactly 200");
    cplus_violation_log_close();

    n = read_file(path_b, buf, sizeof(buf));
    CHECK(n != (size_t)-1, "log B readable");
    CHECK(count_lines(buf) == 200, "log B has exactly 200 lines");
    CHECK(count_prefix(buf, "201 alpha ") == 100, "100 alpha lines present");
    CHECK(count_prefix(buf, "202 beta ") == 100, "100 beta lines present");
    CHECK(valid_concurrency_lines(buf) == 1,
          "all concurrency lines well-formed (no interleaving)");
    CHECK(max_line_len(buf) < 256, "concurrency lines bounded");

    /* (9) failed open returns nonzero and does not change the count */
    {
        unsigned long before = cplus_violation_log_count();
        CHECK(cplus_violation_log_open("no_such_dir_violation/log.log") != 0,
              "open of missing directory fails");
        CHECK(cplus_violation_log_count() == before,
              "failed open does not change count");
    }

    CHECK(guards_intact(&g), "canary guards intact after full run");

    cplus_violation_log_close();
    remove(path_a);
    remove(path_b);

    printf("violation_log: %d %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
