/* Resource-exhaustion resilience: memory pressure WITHOUT host exhaustion.
 *
 * The only injection mechanism used here is stdlib_alloc_fail_* from
 * stdlib/error.hp (fail_at / fail_after / reset).  Every request is a small
 * fixed size (8..64 bytes) or a single over-limit request that the checked
 * allocator refuses by arithmetic before touching the host allocator.  There
 * is deliberately NO "allocate until malloc returns NULL" loop: host memory is
 * never stressed and the process never OOM-kills.
 *
 * The latching-pressure loop runs in a forked child under a 3 s self-alarm and
 * a 6 s parent watchdog (re_watchdog.hp); the parent never blocks on it.  The
 * child communicates only an exit code, so no buffered stdout can be duplicated
 * across the fork.  Injection is reset on every path, including after failure.
 *
 * Self-checking: "<tag> <1|0>" per check, final
 * "RESOURCE_EXHAUSTION <checks> <failures>". Exit 0 only when failures == 0.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "tests/security/resource_exhaustion/support/re_harness.hp"
#include "tests/security/resource_exhaustion/support/re_watchdog.hp"

#define RE_MP_ALLOWED 4
#define RE_MP_ATTEMPTS 12

static int re_mp_child_failures = 0;

/* Child helper: silent checks (no print after fork), counted locally. */
static void re_mp_silent(int cond) {
    if (!cond)
        re_mp_child_failures++;
}

static void re_mp_pressure_child(void) {
    stdlib_telemetry_stats re_mp_tm;
    int re_mp_i;
    void *re_mp_p;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    /* 4 small allocations succeed, attempts 5..12 fail because the injected
     * latching counter is still armed.  Nothing grows beyond 64 bytes. */
    stdlib_alloc_fail_after(RE_MP_ALLOWED);
    for (re_mp_i = 0; re_mp_i < RE_MP_ATTEMPTS; re_mp_i++) {
        errno = 0;
        re_mp_p = stdlib_alloc(64);
        if (re_mp_i < RE_MP_ALLOWED) {
            re_mp_silent(re_mp_p != (void *)0);
            re_mp_silent(stdlib_alloc_seq() == (long)(re_mp_i + 1));
            stdlib_release(re_mp_p);
        } else {
            re_mp_silent(re_mp_p == (void *)0);
            re_mp_silent(stdlib_alloc_seq() == (long)(re_mp_i + 1));
        }
    }

    stdlib_telemetry_snapshot(&re_mp_tm);
    re_mp_silent(re_mp_tm.alloc_count == (unsigned long long)RE_MP_ALLOWED);
    re_mp_silent(re_mp_tm.alloc_failures ==
                 (unsigned long long)(RE_MP_ATTEMPTS - RE_MP_ALLOWED));
    re_mp_silent(re_mp_tm.alloc_count == re_mp_tm.free_count);

    /* Disarm and prove recovery: one more small allocation succeeds. */
    stdlib_alloc_fail_reset();
    re_mp_p = stdlib_alloc(64);
    re_mp_silent(re_mp_p != (void *)0);
    stdlib_release(re_mp_p);
    stdlib_telemetry_set_enabled(0);
    re_child_exit(re_mp_child_failures == 0 ? 0 : 1);
}

int main(void) {
    struct re_watchdog_result re_mp_wd;
    stdlib_telemetry_stats re_mp_before;
    stdlib_telemetry_stats re_mp_after;
    vector re_mp_vec = vector();
    size_t re_mp_cap_before;
    void *re_mp_huge;
    void *re_mp_ok;

    /* ---- 1. forked latching-pressure loop ------------------------------ */
    (void)!fflush(stdout);
    re_mp_wd = re_run_watchdog(re_mp_pressure_child, 6, 3);
    re_check("re_mp_child_reaped", re_mp_wd.reaped == 1);
    re_check("re_mp_child_no_watchdog", re_mp_wd.timed_out == 0);
    re_check("re_mp_child_exited", re_mp_wd.exited == 1);
    re_check("re_mp_child_checks", re_mp_wd.exited == 1 && re_mp_wd.exit_code == 0);

    /* ---- 2. one-shot failure + explicit recovery ------------------------ */
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_at(1);
    re_mp_huge = stdlib_alloc(32);
    re_check("re_mp_oneshot_null", re_mp_huge == (void *)0);
    re_check("re_mp_oneshot_seq", stdlib_alloc_seq() == 1L);
    stdlib_alloc_fail_reset();
    re_mp_ok = stdlib_alloc(32);
    re_check("re_mp_oneshot_recovers", re_mp_ok != (void *)0);
    stdlib_release(re_mp_ok);
    stdlib_release(re_mp_huge); /* NULL is a documented no-op */
    stdlib_telemetry_snapshot(&re_mp_after);
    re_check("re_mp_oneshot_balanced",
             re_mp_after.alloc_count == re_mp_after.free_count);

    /* ---- 3. container growth under injected pressure (rollback) --------- */
    stdlib_telemetry_reset();
    re_mp_vec.push_back(1);
    re_mp_vec.push_back(2);
    re_mp_cap_before = re_mp_vec.capacity();
    stdlib_telemetry_snapshot(&re_mp_before);
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_at(1);
    re_check("re_mp_growth_refused", re_mp_vec.reserve(64) == -1);
    stdlib_alloc_fail_reset();
    re_check("re_mp_growth_state",
             re_mp_vec.size() == 2 && re_mp_vec.capacity() == re_mp_cap_before);
    stdlib_telemetry_snapshot(&re_mp_after);
    re_check("re_mp_growth_no_leak",
             re_mp_after.alloc_count - re_mp_before.alloc_count ==
                 re_mp_after.free_count - re_mp_before.free_count);
    re_check("re_mp_growth_recovers",
             re_mp_vec.reserve(64) == 0 && re_mp_vec.capacity() == 64);
    re_mp_vec.drop();

    /* ---- 4. over-limit / overflow requests are refused by arithmetic ---- */
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();
    re_mp_huge = stdlib_alloc(STDLIB_ALLOC_LIMIT + 1u);
    re_check("re_mp_over_limit_null", re_mp_huge == (void *)0);
    re_check("re_mp_over_limit_no_seq", stdlib_alloc_seq() == 0L);
    re_mp_huge = stdlib_calloc(SIZE_MAX, 2u);
    re_check("re_mp_calloc_overflow_null", re_mp_huge == (void *)0);
    re_check("re_mp_calloc_overflow_no_seq", stdlib_alloc_seq() == 0L);
    stdlib_telemetry_snapshot(&re_mp_after);
    re_check("re_mp_over_limit_no_alloc", re_mp_after.alloc_count == 0ULL);

    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(0);
    re_report();
    return re_harness_failures == 0 ? 0 : 1;
}
