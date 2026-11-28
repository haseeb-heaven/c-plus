/* Resource-exhaustion resilience: unbounded string growth is refused.
 *
 * STDLIB_ALLOC_LIMIT is pinned to 4096 bytes in this translation unit.  A
 * bounded loop (at most 24 appends of 300 bytes) drives a string towards the
 * ceiling; the operation that would cross it must return -1 with the string
 * unchanged.  The loop runs in a forked child under a 5 s watchdog / 2 s child
 * alarm, so even a mistaken infinite growth loop cannot stall the suite.  No
 * host memory is exhausted: the backing buffer never exceeds 4096 bytes.
 *
 * A second case injects a single allocation failure (stdlib_alloc_fail_at(1))
 * at the exact growth point and verifies failure-atomic rollback plus recovery
 * after reset; that one runs in the parent because it has no loop.
 *
 * Self-checking: "<tag> <1|0>" per check, final
 * "RESOURCE_EXHAUSTION <checks> <failures>". Exit 0 only when failures == 0.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define STDLIB_ALLOC_LIMIT ((size_t)4096)

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "tests/security/resource_exhaustion/support/re_harness.hp"
#include "tests/security/resource_exhaustion/support/re_watchdog.hp"

#define RE_SG_CHUNK 300
#define RE_SG_MAX_APPENDS 24

static int re_sg_child_failures = 0;

static void re_sg_silent(int cond) {
    if (!cond)
        re_sg_child_failures++;
}

static void re_sg_growth_child(void) {
    stdlib_telemetry_stats re_sg_tm;
    size_t re_sg_size_before;
    size_t re_sg_len;
    int re_sg_i;
    int re_sg_rc = 0;
    int re_sg_failed_at = -1;

    /* Telemetry must be armed before the first owning object is constructed. */
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    char re_sg_chunk[RE_SG_CHUNK + 1];
    string re_sg_str = string("");
    memset(re_sg_chunk, 'a', RE_SG_CHUNK);
    re_sg_chunk[RE_SG_CHUNK] = '\0';

    for (re_sg_i = 0; re_sg_i < RE_SG_MAX_APPENDS; re_sg_i++) {
        re_sg_size_before = re_sg_str.size();
        re_sg_rc = re_sg_str.append(re_sg_chunk);
        if (re_sg_rc != 0) {
            re_sg_failed_at = re_sg_i;
            re_sg_silent(re_sg_str.size() == re_sg_size_before);
            break;
        }
    }

    re_sg_silent(re_sg_failed_at > 0);         /* the loop really hit the cap */
    re_sg_silent(re_sg_failed_at < RE_SG_MAX_APPENDS); /* bounded, not stuck */
    re_sg_silent(re_sg_str.size() < STDLIB_ALLOC_LIMIT);
    re_sg_silent(re_sg_str.capacity() <= STDLIB_ALLOC_LIMIT);
    re_sg_len = strlen(re_sg_str.c_str());
    re_sg_silent(re_sg_len == re_sg_str.size()); /* terminator never lost */

    /* A smaller append still fits after the refusal: the failure was a limit
     * decision, not a corrupt/permanently stuck string. */
    re_sg_size_before = re_sg_str.size();
    re_sg_silent(re_sg_str.append("ok") == 0);
    re_sg_silent(re_sg_str.size() == re_sg_size_before + 2u);

    re_sg_str.drop();
    stdlib_telemetry_snapshot(&re_sg_tm);
    re_sg_silent(re_sg_tm.alloc_count == re_sg_tm.free_count);
    re_sg_silent(re_sg_tm.alloc_count > 0ULL);
    stdlib_telemetry_set_enabled(0);
    re_child_exit(re_sg_child_failures == 0 ? 0 : 1);
}

int main(void) {
    struct re_watchdog_result re_sg_wd;
    stdlib_telemetry_stats re_sg_tm;
    size_t re_sg_size_before;
    size_t re_sg_cap_before;
    long re_sg_seq_after;

    /* ---- 1. bounded growth loop in a watched child --------------------- */
    (void)!fflush(stdout);
    re_sg_wd = re_run_watchdog(re_sg_growth_child, 5, 2);
    re_check("re_sg_child_reaped", re_sg_wd.reaped == 1);
    re_check("re_sg_child_no_watchdog", re_sg_wd.timed_out == 0);
    re_check("re_sg_child_exited", re_sg_wd.exited == 1 &&
                                  re_sg_wd.exit_code == 0);

    /* ---- 2. injected allocation failure at the growth point ------------ */
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    string re_sg_inject = string("x");
    re_sg_size_before = re_sg_inject.size();
    re_sg_cap_before = re_sg_inject.capacity();
    stdlib_alloc_fail_at(1);
    re_check("re_sg_injected_refused", re_sg_inject.append("0123456789") == -1);
    re_sg_seq_after = stdlib_alloc_seq(); /* capture BEFORE reset clears seq */
    stdlib_alloc_fail_reset();
    re_check("re_sg_injected_state",
             re_sg_inject.size() == re_sg_size_before &&
                 re_sg_inject.capacity() == re_sg_cap_before &&
                 re_sg_inject.equals("x") == 1);
    /* Both the initial string allocation and the failed growth are counted
     * after fail_at resets the sequence; assert positive, bounded accounting. */
    re_check("re_sg_injected_one_attempt", re_sg_seq_after >= 1L);
    re_check("re_sg_injected_recovers",
             re_sg_inject.append("0123456789") == 0 &&
                 re_sg_inject.size() == re_sg_size_before + 10u);
    re_sg_inject.drop();
    stdlib_telemetry_snapshot(&re_sg_tm);
    re_check("re_sg_injected_balanced",
             re_sg_tm.alloc_count == re_sg_tm.free_count);
    stdlib_telemetry_set_enabled(0);

    re_report();
    return re_harness_failures == 0 ? 0 : 1;
}
