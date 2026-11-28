/*
 * Deterministic regressions for Agent 2 libc issue #176 in
 * libc/src/stdlib/atomic_wrappers.c.
 *
 * Valid calls must keep their original C ABI and semantics.  NULL object /
 * expected pointers and invalid memory orders must fail with a deterministic
 * diagnostic (exit 102 for NULL, 108 for an invalid order) instead of a raw
 * SIGSEGV or undefined __atomic_* builtin behaviour.  Guard failures are
 * observed in a forked child so the parent can keep checking.
 *
 * Built by tests/libc/run_libc_tests.sh like every other test_*.c file, or
 * standalone:
 *   clang -O2 -Wall -Wextra -pedantic -std=c99 -D_GNU_SOURCE \
 *     -Ilibc -Ilibc/include -Ilibc/install-include \
 *     tests/libc/test_atomic_wrappers_issue_176.c \
 *     build/libc/libcplus_hardened.a -lm -pthread -o /tmp/atomic_regress
 *   timeout 60 /tmp/atomic_regress
 *
 * Prints exactly:  atomic_wrappers_issue_176: <checks> <failures>
 */
#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "cplus_stdatomic.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++checks;                                                              \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                      \
    } while (0)

/* ---- valid ABI calls are unchanged --------------------------------------- */
static void test_valid_operations(void) {
    int x = 5, expected, prev;
    unsigned int u = 7u;
    void *p = NULL, *ep;
    cplus_atomic_flag flag = CPLUS_ATOMIC_FLAG_INIT;

    CHECK(cplus_atomic_load_int(&x) == 5);
    cplus_atomic_store_explicit_int(&x, 11, CPLUS_MEMORY_ORDER_RELEASE);
    CHECK(cplus_atomic_load_explicit_int(&x, CPLUS_MEMORY_ORDER_ACQUIRE) == 11);
    prev = cplus_atomic_fetch_add_explicit_int(&x, 4, CPLUS_MEMORY_ORDER_RELAXED);
    CHECK(prev == 11);
    CHECK(cplus_atomic_load_int(&x) == 15);

    expected = 15;
    CHECK(cplus_atomic_compare_exchange_strong_explicit_int(
              &x, &expected, 20, CPLUS_MEMORY_ORDER_ACQ_REL,
              CPLUS_MEMORY_ORDER_ACQUIRE) == 1);
    CHECK(cplus_atomic_load_int(&x) == 20);
    expected = 999;
    CHECK(cplus_atomic_compare_exchange_weak_int(&x, &expected, 1) == 0);
    CHECK(expected == 20);
    CHECK(cplus_atomic_exchange_explicit_int(&x, 3, CPLUS_MEMORY_ORDER_SEQ_CST) ==
          20);
    CHECK(cplus_atomic_fetch_sub_int(&x, 1) == 3);
    CHECK(cplus_atomic_fetch_and_int(&x, 2) == 2);
    CHECK(cplus_atomic_fetch_or_int(&x, 1) == 2);
    CHECK(cplus_atomic_fetch_xor_int(&x, 3) == 3);
    CHECK(cplus_atomic_fetch_nand_int(&x, 0) == 0);
    CHECK(cplus_atomic_is_lock_free_int(&x) == 0 ||
          cplus_atomic_is_lock_free_int(&x) == 1);
    CHECK(cplus_kill_dependency_int(x) == x);

    cplus_atomic_init_uint(&u, 9u);
    CHECK(cplus_atomic_load_uint(&u) == 9u);

    ep = p;
    CHECK(cplus_atomic_compare_exchange_strong_ptr(&p, &ep, (void *)&x) == 1);
    CHECK(p == (void *)&x);
    CHECK(cplus_atomic_load_ptr(&p) == (void *)&x);
    CHECK(cplus_kill_dependency_ptr(p) == (void *)&x);

    CHECK(cplus_atomic_flag_test_and_set(&flag) == 0);
    CHECK(cplus_atomic_flag_test_and_set_explicit(
              &flag, CPLUS_MEMORY_ORDER_ACQUIRE) == 1);
    cplus_atomic_flag_clear_explicit(&flag, CPLUS_MEMORY_ORDER_RELEASE);
    CHECK(cplus_atomic_flag_test_and_set(&flag) == 0);
    cplus_atomic_flag_clear(&flag);
    cplus_atomic_thread_fence(CPLUS_MEMORY_ORDER_SEQ_CST);
    cplus_atomic_signal_fence(CPLUS_MEMORY_ORDER_RELAXED);
}

/* ---- guarded failures ----------------------------------------------------- */
static void fail_load_null(void) { (void)cplus_atomic_load_int(NULL); }
static void fail_ptr_load_null(void) { (void)cplus_atomic_load_ptr(NULL); }
static void fail_store_bad_order(void) {
    int x = 0;
    cplus_atomic_store_explicit_int(&x, 1, CPLUS_MEMORY_ORDER_ACQUIRE);
}
static void fail_load_bad_order(void) {
    int x = 0;
    (void)cplus_atomic_load_explicit_int(&x, CPLUS_MEMORY_ORDER_RELEASE);
}
static void fail_cas_null_obj(void) {
    int expected = 0;
    (void)cplus_atomic_compare_exchange_strong_int(NULL, &expected, 1);
}
static void fail_cas_null_expected(void) {
    void *p = NULL;
    (void)cplus_atomic_compare_exchange_strong_ptr(&p, NULL, (void *)&p);
}
static void fail_cas_bad_orders(void) {
    int x = 0, expected = 0;
    (void)cplus_atomic_compare_exchange_strong_explicit_int(
        &x, &expected, 1, CPLUS_MEMORY_ORDER_RELAXED,
        CPLUS_MEMORY_ORDER_SEQ_CST);
}
static void fail_flag_bad_order(void) {
    cplus_atomic_flag flag = CPLUS_ATOMIC_FLAG_INIT;
    cplus_atomic_flag_clear_explicit(&flag, CPLUS_MEMORY_ORDER_ACQUIRE);
}
static void fail_fence_bad_order(void) { cplus_atomic_thread_fence(42); }

/* Runs fn() in a child with stderr captured; the guard must exit with `code`
 * after printing a message containing `needle`. */
static int expect_fatal(void (*fn)(void), int code, const char *needle) {
    int fds[2];
    pid_t pid;
    int status = 0;
    char buf[256];
    ssize_t n;
    size_t total = 0u;

    if (pipe(fds) != 0) return 0;
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return 0;
    }
    if (pid == 0) {
        close(fds[0]);
        (void)dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        fn();
        _Exit(76); /* only reached if the guard did not fire */
    }
    close(fds[1]);
    while (total + 1u < sizeof buf) {
        n = read(fds[0], buf + total, sizeof buf - 1u - total);
        if (n > 0) {
            total += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    buf[total] = '\0';
    close(fds[0]);
    if (waitpid(pid, &status, 0) != pid) return 0;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != code) {
        printf("  exit mismatch: code=%d status=%d output=%s\n", code, status,
               buf);
        return 0;
    }
    if (needle != NULL && strstr(buf, needle) == NULL) {
        printf("  diagnostic mismatch: expected '%s', got '%s'\n", needle, buf);
        return 0;
    }
    return 1;
}

int main(void) {
    test_valid_operations();

    CHECK(expect_fatal(fail_load_null, 102, "null object pointer"));
    CHECK(expect_fatal(fail_ptr_load_null, 102, "null object pointer"));
    CHECK(expect_fatal(fail_cas_null_obj, 102, "null object pointer"));
    CHECK(expect_fatal(fail_cas_null_expected, 102, "null expected pointer"));
    CHECK(expect_fatal(fail_store_bad_order, 108, "invalid memory order"));
    CHECK(expect_fatal(fail_load_bad_order, 108, "invalid memory order"));
    CHECK(expect_fatal(fail_cas_bad_orders, 108, "invalid memory order"));
    CHECK(expect_fatal(fail_flag_bad_order, 108, "invalid memory order"));
    CHECK(expect_fatal(fail_fence_bad_order, 108, "invalid memory order"));

    printf("atomic_wrappers_issue_176: %d %d\n", checks, failures);
    return failures ? 1 : 0;
}
