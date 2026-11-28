/* Resource-exhaustion resilience: thread-creation failure.
 *
 * An actual pthread_create() resource failure is host-dependent, so this
 * fixture first ATTEMPTS a deterministic injection in a forked child: a soft
 * RLIMIT_NPROC of 1 plus a bounded loop of 16 start attempts.  If the platform
 * enforces the limit, the first failure is asserted (STDLIB_FAIL, error code
 * CPLUS_EXIT_RESOURCE, category MEMORY, handle not joinable), the limit is
 * restored, and recovery is asserted.  If all 16 threads start, the case is a
 * declared SKIP ("not injectable on this host") — never a vacuous pass.  The
 * child is bounded by a 6 s watchdog / 2 s alarm and every started thread is
 * joined before exit.  Exit codes: 0 = injected + verified, 1 = failed check,
 * 2 = not injectable (SKIP), 3 = setup error.
 *
 * The parent also covers the two deterministic thread::start error paths that
 * do not need OS injection.
 *
 * Self-checking: "<tag> <1|0>" per check, final
 * "RESOURCE_EXHAUSTION <checks> <failures>". Exit 0 only when failures == 0.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/thread.hp"
#include "tests/security/resource_exhaustion/support/re_harness.hp"
#include "tests/security/resource_exhaustion/support/re_limits.hp"
#include "tests/security/resource_exhaustion/support/re_watchdog.hp"

#define RE_TF_MAX_STARTS 16
#define RE_TF_EXIT_SKIP 2
#define RE_TF_EXIT_SETUP 3

static int re_tf_child_failures = 0;

static void re_tf_silent(int cond) {
    if (!cond)
        re_tf_child_failures++;
}

static void *re_tf_noop(void *arg) {
    (void)arg;
    return NULL;
}

static void re_tf_child(void) {
    struct rlimit re_tf_saved;
    error re_tf_err = error();
    int re_tf_i;
    int re_tf_failed_at = -1;
    int re_tf_rc;

    if (!re_limit_supported(RLIMIT_NPROC) ||
        !re_limit_lower(RLIMIT_NPROC, (rlim_t)1, &re_tf_saved))
        re_child_exit(RE_TF_EXIT_SKIP);

    for (re_tf_i = 0; re_tf_i < RE_TF_MAX_STARTS; re_tf_i++) {
        thread re_tf_worker = thread();
        re_tf_rc = re_tf_worker.start(re_tf_noop, NULL, &re_tf_err);
        if (re_tf_rc != STDLIB_OK) {
            re_tf_failed_at = re_tf_i;
            re_tf_silent(re_tf_rc == STDLIB_FAIL);
            re_tf_silent(re_tf_err.failed_now() == 1);
            re_tf_silent(re_tf_err.code() == CPLUS_EXIT_RESOURCE);
            re_tf_silent(re_tf_err.category() == STDLIB_CATEGORY_MEMORY);
            re_tf_silent(!re_tf_worker.is_joinable());
            re_tf_worker.drop();
            break;
        }
        re_tf_worker.drop(); /* joins the noop thread before the next attempt */
    }

    re_limit_restore(RLIMIT_NPROC, &re_tf_saved);
    if (re_tf_failed_at < 0)
        re_child_exit(RE_TF_EXIT_SKIP);

    /* Recovery after restoring the limit. */
    {
        thread re_tf_retry = thread();
        re_tf_silent(re_tf_retry.start(re_tf_noop, NULL, &re_tf_err) == STDLIB_OK);
        re_tf_silent(re_tf_err.ok() == 1);
        re_tf_retry.drop();
    }
    re_child_exit(re_tf_child_failures == 0 ? 0 : 1);
}

int main(void) {
    struct re_watchdog_result re_tf_wd;
    error re_tf_err = error();
    thread re_tf_parent = thread();
    int re_tf_rc;

    /* ---- 1. bounded injection attempt in a watched child --------------- */
    (void)!fflush(stdout);
    re_tf_wd = re_run_watchdog(re_tf_child, 6, 2);
    re_check("re_tf_child_reaped", re_tf_wd.reaped == 1);
    re_check("re_tf_child_no_watchdog", re_tf_wd.timed_out == 0);
    if (!re_tf_wd.exited) {
        re_check("re_tf_child_exited", 0);
    } else if (re_tf_wd.exit_code == RE_TF_EXIT_SKIP) {
        re_skip("re_tf_injection",
                "pthread_create failure not injectable via RLIMIT_NPROC on this host");
    } else {
        re_check("re_tf_child_exited", re_tf_wd.exit_code == 0 ||
                                       re_tf_wd.exit_code == RE_TF_EXIT_SETUP);
        re_check("re_tf_injected_failure", re_tf_wd.exit_code == 0);
    }

    /* ---- 2. deterministic start() error paths (no OS injection) -------- */
    re_tf_rc = re_tf_parent.start(NULL, NULL, &re_tf_err);
    re_check("re_tf_null_fn_refused", re_tf_rc == STDLIB_FAIL &&
                                     re_tf_err.failed_now() == 1 &&
                                     !re_tf_parent.is_joinable());
    re_tf_rc = re_tf_parent.start(re_tf_noop, NULL, &re_tf_err);
    re_check("re_tf_start_after_failure", re_tf_rc == STDLIB_OK &&
                                         re_tf_err.ok() == 1 &&
                                         re_tf_parent.is_joinable());
    re_tf_rc = re_tf_parent.start(re_tf_noop, NULL, &re_tf_err);
    re_check("re_tf_double_start_refused", re_tf_rc == STDLIB_FAIL &&
                                          re_tf_err.failed_now() == 1);
    re_tf_parent.drop();
    re_check("re_tf_drop_joins", !re_tf_parent.is_joinable());

    re_report();
    return re_harness_failures == 0 ? 0 : 1;
}
