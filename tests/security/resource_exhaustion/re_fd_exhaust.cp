/* Resource-exhaustion resilience: file-descriptor exhaustion.
 *
 * Exhaustion is injected ONLY in a forked child with a lowered soft
 * RLIMIT_NOFILE (baseline + 16, never raising the hard limit).  The child runs
 * a bounded loop of at most 32 open() calls, all on /dev/null, records each
 * descriptor in a fixed array, asserts the refusal errno is EMFILE/ENFILE,
 * closes everything it opened and proves recovery.  No host-wide fd pressure:
 * the parent's descriptors are untouched and the child's limit is restored on
 * every path.  A 6 s parent watchdog plus a 3 s child alarm bound the child.
 *
 * If the platform cannot lower RLIMIT_NOFILE, the child exits with the
 * dedicated SKIP code and the parent emits an explicit SKIP_REASON — never a
 * vacuous pass.  Exit codes: 0 = all child checks ok, 1 = failed check,
 * 2 = injection unsupported (SKIP), 3 = setup error.
 *
 * Self-checking: "<tag> <1|0>" per check, final
 * "RESOURCE_EXHAUSTION <checks> <failures>". Exit 0 only when failures == 0.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include "cplus/cplus.h"
#include "tests/security/resource_exhaustion/support/re_harness.hp"
#include "tests/security/resource_exhaustion/support/re_limits.hp"
#include "tests/security/resource_exhaustion/support/re_watchdog.hp"

#define RE_FE_MAX_OPENS 32
#define RE_FE_EXIT_SKIP 2
#define RE_FE_EXIT_SETUP 3

static int re_fe_child_failures = 0;

static void re_fe_silent(int cond) {
    if (!cond)
        re_fe_child_failures++;
}

static void re_fe_child(void) {
    struct rlimit re_fe_saved;
    rlim_t re_fe_wanted;
    int re_fe_baseline;
    int re_fe_fds[RE_FE_MAX_OPENS];
    int re_fe_opened = 0;
    int re_fe_refused = 0;
    int re_fe_refusal_errno = 0;
    int re_fe_fd;
    int re_fe_i;

    re_fe_baseline = re_fd_count();
    if (re_fe_baseline <= 0)
        re_child_exit(RE_FE_EXIT_SETUP);
    re_fe_wanted = (rlim_t)(re_fe_baseline + 16);
    if (!re_limit_supported(RLIMIT_NOFILE) ||
        !re_limit_lower(RLIMIT_NOFILE, re_fe_wanted, &re_fe_saved))
        re_child_exit(RE_FE_EXIT_SKIP);

    for (re_fe_i = 0; re_fe_i < RE_FE_MAX_OPENS; re_fe_i++) {
        re_fe_fd = open("/dev/null", O_RDONLY);
        if (re_fe_fd >= 0) {
            re_fe_fds[re_fe_opened] = re_fe_fd;
            re_fe_opened++;
        } else {
            re_fe_refused = 1;
            re_fe_refusal_errno = errno;
            break;
        }
    }

    re_fe_silent(re_fe_opened > 0);          /* limit was really lowered */
    re_fe_silent(re_fe_refused == 1);        /* bounded loop hit EMFILE */
    re_fe_silent(re_fe_refusal_errno == EMFILE ||
                 re_fe_refusal_errno == ENFILE);
    re_fe_silent(re_fe_opened < RE_FE_MAX_OPENS); /* never filled the array */

    for (re_fe_i = 0; re_fe_i < re_fe_opened; re_fe_i++)
        (void)!close(re_fe_fds[re_fe_i]);

    /* Recovery after releasing the descriptors, still under the low limit. */
    re_fe_fd = open("/dev/null", O_RDONLY);
    re_fe_silent(re_fe_fd >= 0);
    if (re_fe_fd >= 0)
        (void)!close(re_fe_fd);
    re_fe_silent(re_fd_count() == re_fe_baseline);

    re_limit_restore(RLIMIT_NOFILE, &re_fe_saved);
    re_child_exit(re_fe_child_failures == 0 ? 0 : 1);
}

int main(void) {
    struct re_watchdog_result re_fe_wd;
    int re_fe_baseline;
    int re_fe_after;
    int re_fe_i;

    /* ---- 1. exhaustion in the child ----------------------------------- */
    (void)!fflush(stdout);
    re_fe_wd = re_run_watchdog(re_fe_child, 6, 3);
    re_check("re_fe_child_reaped", re_fe_wd.reaped == 1);
    re_check("re_fe_child_no_watchdog", re_fe_wd.timed_out == 0);
    if (!re_fe_wd.exited) {
        re_check("re_fe_child_exited", 0);
    } else if (re_fe_wd.exit_code == RE_FE_EXIT_SKIP) {
        re_skip("re_fe_exhaust_injection",
                "RLIMIT_NOFILE lowering is unsupported on this host");
    } else {
        re_check("re_fe_child_exited", re_fe_wd.exit_code == 0 ||
                                       re_fe_wd.exit_code == RE_FE_EXIT_SETUP);
        re_check("re_fe_exhaust_refusal", re_fe_wd.exit_code == 0);
    }

    /* ---- 2. parent fd balance across ordinary open/close cycles -------- */
    re_fe_baseline = re_fd_count();
    for (re_fe_i = 0; re_fe_i < 32; re_fe_i++) {
        int re_fe_cycle_fd = open("/dev/null", O_RDONLY);
        if (re_fe_cycle_fd >= 0)
            (void)!close(re_fe_cycle_fd);
    }
    re_fe_after = re_fd_count();
    re_check("re_fd_cycles_stable", re_fe_after == re_fe_baseline);

    re_report();
    return re_harness_failures == 0 ? 0 : 1;
}
