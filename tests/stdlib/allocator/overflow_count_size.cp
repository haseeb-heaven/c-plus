/* Phase 5 / allocation-size arithmetic audit: count * sizeof(element) and
 * array allocation.
 *
 * STDLIB_ALLOC_LIMIT is pinned to 4096 IN SOURCE before error.hp: the C+
 * strict checker does not consume -D flags, so a source-level define is the
 * supported bounded override (documented in OVERFLOW_NOTES.md).  This makes
 * the limit boundary reachable without exhausting memory.
 *
 * For every rejection this file verifies:
 *   - the call returns NULL,
 *   - stdlib_alloc_seq() and telemetry alloc_count are unchanged (the size
 *     check runs BEFORE the allocator call),
 *   - a pre-existing live block is untouched (state unchanged),
 *   - the error category / exit code / message are the documented ones.
 * No system memory is exhausted; SIZE_MAX is only ever passed as a size value.
 *
 * cplus_alloc_array() terminates the process on overflow (it is a raw runtime
 * primitive), so those two probes run in a forked child; the parent asserts
 * exit 107 (CPLUS_EXIT_RESOURCE) and the fatal diagnostic on stderr.
 *
 * Self-checking: "<tag> <0|1>" per check, final "OVERFLOW_COUNT_SIZE <n> <f>".
 */
#ifndef STDLIB_ALLOC_LIMIT
#define STDLIB_ALLOC_LIMIT ((size_t)4096)
#endif

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

#define OCS_LIMIT ((size_t)4096)

static int ocs_checks = 0;
static int ocs_failed = 0;

static void ocs_chk(const char *tag, int cond) {
    ocs_checks++;
    if (!cond)
        ocs_failed++;
    print(tag, cond ? 1 : 0);
}

/* Captured error callback record. */
static int ocs_cb_calls;
static int ocs_cb_cat;
static cplus_exit_code ocs_cb_code;
static char ocs_cb_msg[128];

static void ocs_cb(int cat, cplus_exit_code code, const char *msg) {
    ocs_cb_calls++;
    ocs_cb_cat = cat;
    ocs_cb_code = code;
    if (msg != NULL) {
        size_t n = strlen(msg);
        if (n >= sizeof(ocs_cb_msg))
            n = sizeof(ocs_cb_msg) - 1;
        memcpy(ocs_cb_msg, msg, n);
        ocs_cb_msg[n] = '\0';
    }
}

static void ocs_cb_reset(void) {
    ocs_cb_calls = 0;
    ocs_cb_cat = -1;
    ocs_cb_code = CPLUS_EXIT_OK;
    ocs_cb_msg[0] = '\0';
}

/* Reject-and-verify helper.  `guard` keeps a live block whose contents and
 * recorded size must survive the rejected allocation unchanged. */
static void ocs_reject(const char *tag, size_t count, size_t elem,
                       const char *want_msg) {
    long seq_before, seq_after;
    stdlib_telemetry_stats pre, post;
    void *guard;
    int ok;

    stdlib_alloc_fail_reset();
    guard = stdlib_alloc(64);
    ocs_chk(tag, guard != NULL);
    if (guard == NULL)
        return;
    memset(guard, 0x5a, 64);

    ocs_cb_reset();
    stdlib_telemetry_snapshot(&pre);
    seq_before = stdlib_alloc_seq();
    errno = 0;
    {
        void *p = stdlib_calloc(count, elem);
        ok = p == NULL;
    }
    seq_after = stdlib_alloc_seq();
    stdlib_telemetry_snapshot(&post);
    ok = ok && seq_after == seq_before &&
         post.alloc_count == pre.alloc_count &&
         post.alloc_failures == pre.alloc_failures + 1 &&
         post.api_failures == pre.api_failures + 1 &&
         ocs_cb_calls == 1 && ocs_cb_cat == STDLIB_CATEGORY_MEMORY &&
         ocs_cb_code == CPLUS_EXIT_RESOURCE &&
         strcmp(ocs_cb_msg, want_msg) == 0 &&
         stdlib_alloc_size(guard) == 64 &&
         ((unsigned char *)guard)[0] == 0x5a &&
         ((unsigned char *)guard)[63] == 0x5a;
    ocs_chk(tag, ok);
    stdlib_release(guard);
}

static void ocs_count_size(void) {
    void *full, *zero;
    long seq_before, seq_after;

    /* count * sizeof(element) overflow: __builtin_mul_overflow rejects all
     * of these before stdlib_alloc() is entered. */
    ocs_reject("ocs_mul_sxmax_x4", SIZE_MAX, sizeof(int),
               "allocation size overflow");
    ocs_reject("ocs_mul_2_xsxmax", (size_t)2, SIZE_MAX,
               "allocation size overflow");
    ocs_reject("ocs_mul_halfplus_x2", (SIZE_MAX / (size_t)2) + 1u, (size_t)2,
               "allocation size overflow");
    ocs_reject("ocs_mul_sxmax_xsxmax", SIZE_MAX, SIZE_MAX,
               "allocation size overflow");
    ocs_reject("ocs_mul_intmax_xintmax", (size_t)INT_MAX, (size_t)INT_MAX,
               "allocation size overflow");

    /* Over-limit (no wrap) is refused with the same category/code/message. */
    ocs_reject("ocs_mul_over_limit", OCS_LIMIT / sizeof(int) + 1u, sizeof(int),
               "allocation size overflow");

    /* Exact boundary: LIMIT bytes is accepted; metadata records count*size
     * as the payload size and the element size, with no hidden header. */
    seq_before = stdlib_alloc_seq();
    full = stdlib_calloc(OCS_LIMIT / sizeof(int), sizeof(int));
    seq_after = stdlib_alloc_seq();
    ocs_chk("ocs_boundary_alloc", full != NULL && seq_after == seq_before + 1 &&
                                      stdlib_alloc_size(full) == OCS_LIMIT &&
                                      stdlib_alloc_elem_size(full) == sizeof(int) &&
                                      stdlib_alloc_length(full) ==
                                          OCS_LIMIT / sizeof(int));
    if (full != NULL) {
        memset(full, 0, OCS_LIMIT);
        stdlib_release(full);
    }

    /* Zero count or zero element size maps to one tracked byte; an extreme
     * element size with count == 0 is explicitly defined, not rejected. */
    seq_before = stdlib_alloc_seq();
    zero = stdlib_calloc((size_t)0, SIZE_MAX);
    ocs_chk("ocs_zero_count", zero != NULL && stdlib_alloc_seq() == seq_before + 1 &&
                                  stdlib_alloc_size(zero) == 1 &&
                                  stdlib_alloc_elem_size(zero) == SIZE_MAX);
    if (zero != NULL)
        stdlib_release(zero);
    zero = stdlib_calloc(SIZE_MAX, (size_t)0);
    ocs_chk("ocs_zero_elem", zero != NULL && stdlib_alloc_size(zero) == 1);
    if (zero != NULL)
        stdlib_release(zero);
}

/* Forked probe for the raw runtime array allocator.  On multiplication
 * overflow cplus_alloc_array() logs and calls cplus_print_fatal() BEFORE
 * cplus_zalloc(), so no allocation is attempted and the child exits with
 * CPLUS_EXIT_RESOURCE (107). */
static void ocs_fatal_probe(const char *tag, size_t count, size_t elem) {
    pid_t pid;
    int status = 0;
    int rc;

    fflush(NULL);
    pid = fork();
    if (pid == 0) {
        void *p = cplus_alloc_array(count, elem);
        if (p != NULL)
            cplus_release(p);
        _Exit(0);
    }
    if (pid < 0) {
        ocs_chk(tag, 0);
        return;
    }
    rc = waitpid(pid, &status, 0);
    ocs_chk(tag, rc == pid && WIFEXITED(status) &&
                     WEXITSTATUS(status) == (int)CPLUS_EXIT_RESOURCE);
}

static void ocs_array_alloc(void) {
    long seq_before, seq_after;
    stdlib_telemetry_stats pre, post;

    stdlib_telemetry_snapshot(&pre);
    seq_before = stdlib_alloc_seq();
    ocs_fatal_probe("ocs_array_mul_sxmax", SIZE_MAX, sizeof(int));
    ocs_fatal_probe("ocs_array_mul_halfplus",
                    (SIZE_MAX / sizeof(int)) + 1u, sizeof(int));
    seq_after = stdlib_alloc_seq();
    stdlib_telemetry_snapshot(&post);
    ocs_chk("ocs_array_no_parent_alloc",
            seq_after == seq_before && post.alloc_count == pre.alloc_count);
}

int main(void) {
    stdlib_set_error_callback(ocs_cb);
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();
    ocs_cb_reset();
    ocs_count_size();
    ocs_array_alloc();
    stdlib_set_error_callback(NULL);
    print("OVERFLOW_COUNT_SIZE", ocs_checks);
    print("OVERFLOW_COUNT_SIZE_FAILURES", ocs_failed);
    return ocs_failed == 0 ? 0 : 1;
}
