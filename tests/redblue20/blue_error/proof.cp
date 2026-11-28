/* BLUE-TEAM agent 17/20 - error handling and runtime liveness safety proof.
 *
 * Fork-per-case harness: the parent forks one fresh child per fixed-seed case,
 * bounds it with a SIGALRM watchdog, and reaps it.  The child runs exactly one
 * case and reports PASS/FAIL through its exit code.  Threads are created only
 * via the managed `thread` class inside the one case that needs them, and only
 * inside its own forked child, so the parent never forks after threads exist.
 *
 * Properties proved (all deterministic, fixed seed, no game assets):
 *   0  stale generation refusal        (stdlib_alloc_check_generation)
 *   1  recycled-address detection      (generation fencing across reuse)
 *   2  metadata cache behavior          (record/forget/find, growth, tombs)
 *   3  error-callback thread safety     (per-thread guard + atomic counters)
 *   4  ownership transfer validation    (stdlib_transfer_ownership rules)
 *   5  telemetry consistency            (exact atomic counter accounting)
 *
 * Summary line: `BLUE20 error <cases> <passed> <failed>`; exit 0 iff failed==0.
 * Bounded wall clock via the caller's `timeout 60` (run.sh).
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/thread.hp"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define NCASES 2400u
#define BASE_SEED 0x5EED1234u
#define CASE_WATCHDOG_SECS 10u

static int g_case;

/* Forward declarations: dispatcher below calls the case bodies. */
static int case_stale_gen(void);
static int case_recycled(void);
static int case_meta_cache(void);
static int case_callback(void);
static int case_ownership(void);
static int case_telemetry(void);

/* ---- deterministic PRNG (xorshift32, no variable division/modulo) -------- */
static unsigned int rng_state;

static void seed_rng(unsigned int s) {
    rng_state = s | 1u;
}

static unsigned int rnd(void) {
    unsigned int x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

/* ---- failure reporting --------------------------------------------------- */
static int fail_here(int code) {
    print("CASEFAIL", g_case, code);
    (void)!fflush(stdout);
    return 0;
}

static int dispatch_case(unsigned int index) {
    unsigned int seed = BASE_SEED ^ (index * 0x9e3779b9u);
    g_case = (int)index;
    seed_rng(seed);
    switch (index % 6u) {
    case 0u: return case_stale_gen();
    case 1u: return case_recycled();
    case 2u: return case_meta_cache();
    case 3u: return case_callback();
    case 4u: return case_ownership();
    default: return case_telemetry();
    }
}

static void case_runner(unsigned int index) {
    int result = dispatch_case(index);
    _Exit(result ? 0 : 1);
}

/* ---- bounded process watchdog ------------------------------------------- */
struct wd_result {
    int reaped;
    int timed_out;
    int exited;
    int exit_code;
    int signaled;
    int term_signal;
};

static volatile sig_atomic_t wd_alarm;

static void wd_on_alarm(int signo) {
    (void)signo;
    wd_alarm = 1;
}

static struct wd_result run_watchdog(unsigned int index, unsigned int hard_secs) {
    struct wd_result r;
    struct sigaction old_action;
    struct sigaction backstop;
    pid_t pid;
    pid_t wrc;
    int status;
    int reaped;
    int timed_out;

    r.reaped = 0;
    r.timed_out = 0;
    r.exited = 0;
    r.exit_code = -1;
    r.signaled = 0;
    r.term_signal = 0;
    status = 0;

    (void)!fflush(stdout);
    pid = fork();
    if (pid == (pid_t)0) {
        case_runner(index);
        _Exit(1);
    }
    if (pid < (pid_t)0) {
        r.timed_out = 1;
        return r;
    }

    memset(&backstop, 0, sizeof(backstop));
    backstop.sa_handler = wd_on_alarm;
    sigemptyset(&backstop.sa_mask);
    backstop.sa_flags = 0;
    if (sigaction(SIGALRM, &backstop, &old_action) != 0) {
        (void)!kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < (pid_t)0) {
            /* bounded reap */
        }
        r.reaped = 1;
        r.timed_out = 1;
        reaped = 1;
        timed_out = 1;
    } else {
        wd_alarm = 0;
        (void)!alarm(hard_secs);
        reaped = 0;
        timed_out = 0;
        for (;;) {
            wrc = waitpid(pid, &status, 0);
            if (wrc == pid) {
                reaped = 1;
                break;
            }
            if (wrc < (pid_t)0 && errno == EINTR) {
                if (wd_alarm) {
                    timed_out = 1;
                    break;
                }
                continue;
            }
            if (wrc < (pid_t)0)
                break;
        }
        (void)!alarm(0);
        (void)!sigaction(SIGALRM, &old_action, (struct sigaction *)0);

        if (!reaped) {
            timed_out = 1;
            (void)!kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < (pid_t)0) {
                /* bounded reap */
            }
            reaped = 1;
        }
        r.reaped = reaped;
        r.timed_out = timed_out;
    }

    if (WIFEXITED(status)) {
        r.exited = 1;
        r.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        r.signaled = 1;
        r.term_signal = WTERMSIG(status);
    }
    return r;
}

/* ---- property 0: stale generation refusal -------------------------------- */
static int case_stale_gen(void) {
    unsigned int s = rnd();
    size_t sz = 64u + (size_t)(s & 63u);
    size_t nsz = 128u + (size_t)((s >> 8) & 127u);
    void *p = stdlib_alloc(sz);
    unsigned long long g0;
    void *q;
    unsigned long long g1;

    if (p == (void *)0) return fail_here(1);
    g0 = stdlib_alloc_generation(p);
    if (g0 == 0ull) { stdlib_release(p); return fail_here(2); }
    if (!stdlib_alloc_check_generation(p, g0)) { stdlib_release(p); return fail_here(3); }
    if (stdlib_alloc_check_generation(p, g0 + 7ull)) { stdlib_release(p); return fail_here(4); }
    if (stdlib_alloc_check_generation((void *)0, g0)) { stdlib_release(p); return fail_here(5); }

    q = stdlib_realloc(p, nsz);
    if (q == (void *)0) { stdlib_release(p); return fail_here(6); }
    g1 = stdlib_alloc_generation(q);
    if (g1 == 0ull || g1 == g0) { stdlib_release(q); return fail_here(7); }
    if (stdlib_alloc_check_generation(q, g0)) { stdlib_release(q); return fail_here(8); }
    if (!stdlib_alloc_check_generation(q, g1)) { stdlib_release(q); return fail_here(9); }
    if (stdlib_alloc_check_generation(p, g0)) { stdlib_release(q); return fail_here(10); }
    if (stdlib_alloc_size(p) != 0u) { stdlib_release(q); return fail_here(11); }
    if (stdlib_alloc_size(q) != nsz) { stdlib_release(q); return fail_here(12); }
    stdlib_release(q);
    return 1;
}

/* ---- property 1: recycled-address detection ------------------------------ */
#define RPOOL 128
static int case_recycled(void) {
    void *oldp[RPOOL];
    unsigned long long oldg[RPOOL];
    unsigned int s = rnd();
    size_t sz = 32u + (size_t)(s & 31u);
    int i;
    int j;
    int recycled = 0;

    for (i = 0; i < RPOOL; ++i) {
        oldp[i] = stdlib_alloc(sz);
        if (oldp[i] == (void *)0) return fail_here(20);
        oldg[i] = stdlib_alloc_generation(oldp[i]);
        if (oldg[i] == 0ull) return fail_here(21);
    }
    for (i = 0; i < RPOOL; ++i) stdlib_release(oldp[i]);

    for (i = 0; i < RPOOL; ++i) {
        void *n = stdlib_alloc(sz);
        unsigned long long ng;
        if (n == (void *)0) return fail_here(22);
        ng = stdlib_alloc_generation(n);
        if (ng == 0ull) return fail_here(23);
        for (j = 0; j < RPOOL; ++j) {
            if (n == oldp[j]) {
                ++recycled;
                if (ng == oldg[j]) return fail_here(24);
                if (stdlib_alloc_check_generation(n, oldg[j])) return fail_here(25);
            }
            if (stdlib_alloc_check_generation(n, oldg[j])) return fail_here(26);
        }
        if (!stdlib_alloc_check_generation(n, ng)) return fail_here(27);
        stdlib_release(n);
    }
    if (recycled == 0) return fail_here(28);
    return 1;
}

/* ---- property 2: metadata cache behavior -------------------------------- */
#define MC_N 96
static const size_t mc_elts[4] = { 1u, 2u, 4u, 8u };

static unsigned int mc_pick(unsigned int s, int shift) {
    switch (shift) {
    case 0: return s & 63u;
    case 1: return (s >> 8) & 63u;
    case 2: return (s >> 16) & 63u;
    default: return (s >> 24) & 63u;
    }
}

static int case_meta_cache(void) {
    void *ptrs[MC_N];
    size_t sizes[MC_N];
    size_t lens[MC_N];
    size_t elems[MC_N];
    unsigned long long gens[MC_N];
    unsigned int s = rnd();
    int i;

    for (i = 0; i < MC_N; ++i) {
        size_t base = 16u + (mc_pick(s, i & 3) * 8u);
        sizes[i] = base;
        if ((i & 3) == 0) {
            size_t cnt = 1u + ((s >> 8) & 15u);
            size_t el = mc_elts[(s >> 20) & 3u];
            elems[i] = el;
            ptrs[i] = stdlib_calloc(cnt, el);
            sizes[i] = cnt * el;
            lens[i] = cnt;
        } else {
            elems[i] = 1u;
            ptrs[i] = stdlib_alloc(base);
            lens[i] = base;
        }
        if (ptrs[i] == (void *)0) return fail_here(40);
        gens[i] = stdlib_alloc_generation(ptrs[i]);
        if (gens[i] == 0ull) return fail_here(41);
    }

    for (i = 0; i < MC_N; ++i) {
        size_t out = 12345u;
        if (stdlib_alloc_size(ptrs[i]) != sizes[i]) return fail_here(42);
        if (stdlib_alloc_elem_size(ptrs[i]) != elems[i]) return fail_here(43);
        if (stdlib_alloc_length(ptrs[i]) != lens[i]) return fail_here(44);
        if (stdlib_alloc_query_length(ptrs[i], elems[i], &out) != 0) return fail_here(45);
        if (out != lens[i]) return fail_here(46);
        if (!stdlib_alloc_check_generation(ptrs[i], gens[i])) return fail_here(47);
    }

    for (i = 0; i < MC_N; i += 2) {
        size_t out = 999u;
        stdlib_release(ptrs[i]);
        if (stdlib_alloc_size(ptrs[i]) != 0u) return fail_here(48);
        if (stdlib_alloc_check_generation(ptrs[i], gens[i])) return fail_here(49);
        if (stdlib_alloc_query_length(ptrs[i], 1u, &out) != -1) return fail_here(50);
        if (out != 0u) return fail_here(51);
    }
    for (i = 1; i < MC_N; i += 2) {
        if (stdlib_alloc_size(ptrs[i]) != sizes[i]) return fail_here(52);
        if (!stdlib_alloc_check_generation(ptrs[i], gens[i])) return fail_here(53);
        stdlib_release(ptrs[i]);
    }
    return 1;
}

/* ---- property 3: error-callback thread safety ---------------------------- */
static volatile int cb_counter;
static volatile int cb_outer;

static void cb_count(int cat, cplus_exit_code code, const char *msg) {
    (void)cat; (void)code; (void)msg;
    __atomic_fetch_add(&cb_counter, 1, __ATOMIC_RELAXED);
}

static void cb_reentrant(int cat, cplus_exit_code code, const char *msg) {
    (void)cat; (void)code; (void)msg;
    __atomic_fetch_add(&cb_outer, 1, __ATOMIC_RELAXED);
    /* Nested note must be dropped by the per-thread guard; recursion would
     * crash the child and the case would be scored as a failure. */
    stdlib_note(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_IO, "nested");
}

static void *cb_worker(void *arg) {
    (void)arg;
    stdlib_note(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_IO, "thread");
    return (void *)0;
}

#define CB_THREADS 4
static int case_callback(void) {
    thread wt0 = thread();
    thread wt1 = thread();
    thread wt2 = thread();
    thread wt3 = thread();
    error thr_err = error();
    int r0;
    int r1;
    int r2;
    int r3;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();

    stdlib_set_error_callback(cb_reentrant);
    __atomic_store_n(&cb_outer, 0, __ATOMIC_RELAXED);
    stdlib_note(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_IO, "outer");
    if (__atomic_load_n(&cb_outer, __ATOMIC_RELAXED) != 1) {
        stdlib_set_error_callback((stdlib_error_callback)0);
        return fail_here(60);
    }

    __atomic_store_n(&cb_counter, 0, __ATOMIC_RELAXED);
    stdlib_set_error_callback(cb_count);
    r0 = wt0.start(cb_worker, (void *)0, &thr_err);
    r1 = wt1.start(cb_worker, (void *)0, &thr_err);
    r2 = wt2.start(cb_worker, (void *)0, &thr_err);
    r3 = wt3.start(cb_worker, (void *)0, &thr_err);
    if (r0 == STDLIB_OK) (void)wt0.join((void **)0, &thr_err);
    if (r1 == STDLIB_OK) (void)wt1.join((void **)0, &thr_err);
    if (r2 == STDLIB_OK) (void)wt2.join((void **)0, &thr_err);
    if (r3 == STDLIB_OK) (void)wt3.join((void **)0, &thr_err);

    if (r0 != STDLIB_OK || r1 != STDLIB_OK || r2 != STDLIB_OK || r3 != STDLIB_OK ||
        __atomic_load_n(&cb_counter, __ATOMIC_RELAXED) != CB_THREADS) {
        stdlib_set_error_callback((stdlib_error_callback)0);
        return fail_here(61);
    }
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(0);
    return 1;
}

/* ---- property 4: ownership transfer validation -------------------------- */
static int case_ownership(void) {
    void *nul = (void *)0;
    void *p = stdlib_alloc(64u);
    void *dst = (void *)0;
    void *d2 = (void *)0;
    void *src2;
    void *dst2;
    void *stale;
    unsigned long long g0;
    unsigned long long g1;

    if (p == nul) return fail_here(70);
    g0 = stdlib_alloc_generation(p);
    if (stdlib_transfer_ownership(&dst, &p) != 0 || p != nul || dst == nul) {
        if (dst != nul) stdlib_release(dst); else stdlib_release(p);
        return fail_here(71);
    }
    g1 = stdlib_alloc_generation(dst);
    if (g1 == 0ull || g1 == g0) { stdlib_release(dst); return fail_here(72); }
    if (stdlib_alloc_check_generation(dst, g0)) { stdlib_release(dst); return fail_here(73); }
    if (!stdlib_alloc_check_generation(dst, g1)) { stdlib_release(dst); return fail_here(74); }

    if (stdlib_transfer_ownership((void **)0, &dst) == 0) { stdlib_release(dst); return fail_here(75); }
    if (stdlib_transfer_ownership(&d2, (void **)0) == 0) { stdlib_release(dst); return fail_here(76); }
    if (stdlib_transfer_ownership(&d2, &nul) == 0) { stdlib_release(dst); return fail_here(77); }

    src2 = dst;
    dst2 = dst; /* non-NULL destination */
    if (stdlib_transfer_ownership(&dst2, &src2) == 0) { stdlib_release(dst); return fail_here(78); }
    if (src2 != dst || dst2 != dst) { stdlib_release(dst); return fail_here(79); }

    if (stdlib_transfer_ownership(&dst, &dst) == 0) { stdlib_release(dst); return fail_here(80); }

    stale = stdlib_alloc(32u);
    if (stale == nul) { stdlib_release(dst); return fail_here(81); }
    stdlib_release(stale);
    if (stdlib_transfer_ownership(&d2, &stale) == 0) { stdlib_release(dst); return fail_here(82); }
    if (d2 != nul) { stdlib_release(dst); return fail_here(83); }

    stdlib_release(dst);
    return 1;
}

/* ---- property 5: telemetry consistency ---------------------------------- */
static int case_telemetry(void) {
    stdlib_telemetry_stats st;
    unsigned int s = rnd();
    size_t s1 = 33u + (size_t)(s & 31u);
    size_t s2 = 65u + (size_t)((s >> 8) & 63u);
    size_t s3 = 17u + (size_t)((s >> 16) & 15u);
    void *a;
    void *b;
    void *c;
    void *big;
    void *f;
    void *rr;
    void *src;
    void *dst;
    int rc;

    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();

    a = stdlib_alloc(s1);
    b = stdlib_alloc(s2);
    c = stdlib_alloc(s3);
    if (a == (void *)0 || b == (void *)0 || c == (void *)0) return fail_here(90);
    stdlib_release(c);

    stdlib_note(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_IO, "x");
    big = stdlib_alloc(STDLIB_ALLOC_LIMIT + 1u);
    if (big != (void *)0) return fail_here(91);

    stdlib_alloc_fail_at(1);
    f = stdlib_alloc(16u);
    stdlib_alloc_fail_reset();
    if (f != (void *)0) return fail_here(92);

    src = a;
    dst = b; /* non-NULL destination -> invalid ownership transfer */
    rc = stdlib_transfer_ownership(&dst, &src);
    if (rc == 0) return fail_here(93); /* non-NULL dst must be rejected */

    rr = stdlib_realloc(b, s2 + 32u);
    if (rr == (void *)0) return fail_here(94);

    stdlib_telemetry_snapshot(&st);
    if (st.alloc_count != 3ull) return fail_here(95);
    if (st.free_count != 1ull) return fail_here(96);
    if (st.bytes_allocated != (unsigned long long)(s1 + s2 + s3)) return fail_here(97);
    if (st.alloc_failures != 2ull) return fail_here(98);
    if (st.invalid_ops != 1ull) return fail_here(99);
    if (st.ownership_violations != 1ull) return fail_here(100);
    if (st.api_failures != 4ull) return fail_here(101);
    if (st.container_growths != 1ull) return fail_here(102);
    if (st.bounds_violations != 0ull) return fail_here(103);
    if (st.lifecycle_failures != 0ull) return fail_here(104);

    stdlib_telemetry_set_enabled(0);
    stdlib_telemetry_reset();
    (void)stdlib_alloc(7u);
    stdlib_note(STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE, "off");
    stdlib_telemetry_snapshot(&st);
    if (st.alloc_count != 0ull || st.api_failures != 0ull ||
        st.alloc_failures != 0ull || st.bytes_allocated != 0ull)
        return fail_here(105);

    stdlib_release(a);
    stdlib_release(rr);
    return 1;
}

/* ---- driver --------------------------------------------------------------- */
int main(void) {
    unsigned int i;
    unsigned int passed = 0u;
    unsigned int failed = 0u;

    for (i = 0u; i < NCASES; ++i) {
        struct wd_result r = run_watchdog(i, (unsigned int)CASE_WATCHDOG_SECS);
        if (r.exited != 0 && r.exit_code == 0 && r.timed_out == 0) {
            ++passed;
        } else {
            ++failed;
            print("CASE_REAP_FAIL", i, r.exit_code, r.timed_out, r.signaled,
                  r.term_signal);
            (void)!fflush(stdout);
        }
    }
    print("BLUE20 error", NCASES, passed, failed);
    (void)!fflush(stdout);
    return failed == 0u ? 0 : 1;
}
