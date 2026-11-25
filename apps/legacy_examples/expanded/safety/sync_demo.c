/* Safety demo 4: additive CHECKED sync wrappers from
 * build/libc/libcplus_hardened.a (C99).
 *
 * The legacy cplus_mtx_destroy/cplus_cnd_destroy are void forwarders that
 * discard the pthread return code, and cplus_thrd_join has no double-join
 * guard.  The _checked variants add a bounded side-table guard and refuse a
 * second destroy/join with EINVAL BEFORE calling pthread -- no crash, no UB.
 *
 * Build (from repo root):
 *   /usr/bin/clang -std=c99 -D_POSIX_C_SOURCE=200809L -I. -Ilibc/install-include \
 *     examples/expanded/safety/sync_demo.c \
 *     build/libc/libcplus_hardened.a -pthread \
 *     -o examples/expanded/safety/sync_demo
 *
 * Output: deterministic "sync <case> OK|REFUSED <detail>" lines.
 * Exit 0 iff every expected refusal was observed (EINVAL on the repeat/NULL
 * calls) and every valid operation succeeded.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../../libc/install-include/cplus_threads.h"
#include "../../../libc/install-include/cplus_sync_checked.h"

static int g_failed = 0;

static void mark(const char *case_name, const char *got, const char *want,
                 const char *detail) {
    printf("sync %s %s %s\n", case_name, got, detail);
    if (strcmp(got, want) != 0)
        g_failed = 1;
    fflush(stdout);
}

static int worker(void *arg) {
    (void)arg;
    return 42;
}

int main(void) {
    cplus_mtx_t m;
    cplus_cnd_t c;
    cplus_thrd_t t;
    cplus_thrd_t zero;
    int res = -1;
    int rc1, rc2;
    char detail[128];

    /* ---- mutex: valid destroy, then a double-destroy refusal ------------- */
    rc1 = cplus_mtx_init(&m, CPLUS_MTX_PLAIN);
    rc1 = (rc1 == CPLUS_THRD_SUCCESS) ? cplus_mtx_destroy_checked(&m) : -1;
    rc2 = cplus_mtx_destroy_checked(&m);
    snprintf(detail, sizeof(detail), "init_destroy=%d double=%d (EINVAL=%d)",
             rc1, rc2, EINVAL);
    mark("mtx_destroy", (rc1 == 0 && rc2 == EINVAL) ? "REFUSED" : "OK",
         "REFUSED", detail);

    /* ---- condition variable: same pattern ------------------------------- */
    rc1 = cplus_cnd_init(&c);
    rc1 = (rc1 == CPLUS_THRD_SUCCESS) ? cplus_cnd_destroy_checked(&c) : -1;
    rc2 = cplus_cnd_destroy_checked(&c);
    snprintf(detail, sizeof(detail), "init_destroy=%d double=%d (EINVAL=%d)",
             rc1, rc2, EINVAL);
    mark("cnd_destroy", (rc1 == 0 && rc2 == EINVAL) ? "REFUSED" : "OK",
         "REFUSED", detail);

    /* ---- thread: valid join, then a double-join refusal ------------------ */
    rc1 = cplus_thrd_create(&t, worker, NULL);
    rc1 = (rc1 == CPLUS_THRD_SUCCESS) ? cplus_thrd_join_checked(t, &res) : -1;
    rc2 = cplus_thrd_join_checked(t, &res);
    snprintf(detail, sizeof(detail), "join=%d result=%d double=%d (EINVAL=%d)",
             rc1, res, rc2, EINVAL);
    mark("thrd_join", (rc1 == 0 && res == 42 && rc2 == EINVAL)
             ? "REFUSED" : "OK", "REFUSED", detail);

    /* ---- NULL / zeroed handles are refused, never dereferenced ---------- */
    memset(&zero, 0, sizeof zero);
    rc1 = cplus_mtx_destroy_checked(NULL);
    rc2 = cplus_cnd_destroy_checked(NULL);
    {
        int rc3 = cplus_thrd_join_checked(zero, &res);
        snprintf(detail, sizeof(detail),
                 "mtx_NULL=%d cnd_NULL=%d thrd_zero=%d (EINVAL=%d)",
                 rc1, rc2, rc3, EINVAL);
        mark("null_handles", (rc1 == EINVAL && rc2 == EINVAL && rc3 == EINVAL)
                 ? "REFUSED" : "OK", "REFUSED", detail);
    }

    return g_failed;
}
