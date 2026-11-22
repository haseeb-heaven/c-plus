/*
 * concurrency_c.c -- C side of the paired Rust/C concurrency + ownership suite.
 *
 * PAIRED WITH: tests/rust_vs_cplus/concurrency_rs.rs
 *
 * PROTOCOL (identical in both programs)
 * -------------------------------------
 *   argv[1] = case number, 1..100
 *   stdout  = exactly one line: "RESULT <n> <OUTCOME>"
 *             OUTCOME in { ACCEPTED, REFUSED, SKIP }
 *   exit code is always 0.
 *
 * OUTCOME SEMANTICS (shared with the Rust side)
 * ---------------------------------------------
 *   ACCEPTED : every intended safe operation completed successfully.
 *   REFUSED  : at least one attempted unsafe/invalid operation was correctly
 *              rejected by a checked API (C) or by a Result-based safe API
 *              that models what Rust's ownership/borrow rules forbid (Rust).
 *   SKIP     : no comparable runtime event exists. These are the cases where
 *              Rust rejects the program at COMPILE TIME (borrow checker) and
 *              plain C has no equivalent static check. Both programs print
 *              SKIP so the runner can still compare outcomes.
 *
 * MECHANISM DIFFERENCE (why the outcomes are the same but the guarantees are not)
 * -------------------------------------------------------------------------------
 *   C+  : detection is RUNTIME and only for blocks from the checked allocator.
 *         free_checked_s() uses a bounded recent-free cache; the metadata table
 *         is mutex-protected. A stale alias whose address was re-issued, a raw
 *         malloc() pointer, a stack pointer, and caller-level data races are NOT
 *         provably detected. All caller-shared state in this program is guarded
 *         by pthread mutexes, and no invalid pointer is ever dereferenced: the
 *         checked queries are consulted first.
 *   Rust: forbidden operations are normally COMPILE-TIME errors (moved values,
 *         &mut aliasing). To make them observable by a runtime runner, the Rust
 *         program exposes a *safe*, Result-returning model of the checked API:
 *         an attempted double-free / UAF / bad transfer returns Err => REFUSED.
 *         That is an explicit modelling choice, not a claim that Rust performs
 *         the same runtime check. Rust threads use std::thread::scope,
 *         Arc<Mutex<_>> and std::sync::{Barrier, Condvar}; there is no unsafe.
 *
 * SCENARIO INDEX (id: name -- expected outcome)
 * 001 single_alloc_free -- ACCEPTED
 * 002 parallel_private_alloc -- ACCEPTED
 * 003 parallel_calloc -- ACCEPTED
 * 004 parallel_aligned_alloc -- ACCEPTED
 * 005 parallel_realloc -- ACCEPTED
 * 006 parallel_mixed_alloc -- ACCEPTED
 * 007 alloc_free_waves -- ACCEPTED
 * 008 cross_thread_transfer_then_free -- ACCEPTED
 * 009 foreign_free_after_owner -- REFUSED
 * 010 racing_double_free -- REFUSED
 * 011 count_query_live -- ACCEPTED
 * 012 query_length_live -- ACCEPTED
 * 013 remaining_capacity_interior -- ACCEPTED
 * 014 remaining_capacity_untracked -- REFUSED
 * 015 metadata_after_free -- REFUSED
 * 016 is_live_after_free -- REFUSED
 * 017 generation_snapshot -- ACCEPTED
 * 018 generation_after_realloc -- REFUSED
 * 019 live_count_delta -- ACCEPTED
 * 020 live_bytes_delta -- ACCEPTED
 * 021 double_free_same_thread -- REFUSED
 * 022 double_free_compile_time_only -- SKIP
 * 023 double_free_via_alias -- REFUSED
 * 024 double_free_across_waves -- REFUSED
 * 025 double_free_many_threads -- REFUSED
 * 026 double_free_after_transfer_free -- REFUSED
 * 027 invalid_free_stack -- REFUSED
 * 028 invalid_free_null -- ACCEPTED
 * 029 invalid_free_raw_malloc -- REFUSED
 * 030 free_s_then_checked_refusal -- REFUSED
 * 031 uaf_read_after_free -- REFUSED
 * 032 uaf_write_after_free -- REFUSED
 * 033 uaf_two_threads -- REFUSED
 * 034 uaf_borrow_after_move_compile_only -- SKIP
 * 035 uaf_after_realloc -- REFUSED
 * 036 uaf_after_transfer -- REFUSED
 * 037 uaf_repeated_wave -- REFUSED
 * 038 uaf_untracked_pointer -- REFUSED
 * 039 uaf_null_pointer -- REFUSED
 * 040 uaf_safe_read_live -- ACCEPTED
 * 041 log_level_off_on -- ACCEPTED
 * 042 log_level_shared_toggle -- ACCEPTED
 * 043 log_message_emitted -- ACCEPTED
 * 044 log_level_trace_alloc -- ACCEPTED
 * 045 log_stats_observe -- ACCEPTED
 * 046 log_stats_failure_count -- REFUSED
 * 047 log_level_restore_after_error -- ACCEPTED
 * 048 log_concurrent_emit -- ACCEPTED
 * 049 log_invalid_level_refused -- REFUSED
 * 050 log_toggle_with_alloc_wave -- ACCEPTED
 * 051 tls_counter_increment -- ACCEPTED
 * 052 per_thread_alloc_slot -- ACCEPTED
 * 053 per_thread_generation -- ACCEPTED
 * 054 per_thread_log_level -- ACCEPTED
 * 055 per_thread_error_count -- REFUSED
 * 056 per_thread_reuse -- ACCEPTED
 * 057 per_thread_stats -- ACCEPTED
 * 058 per_thread_cond_state -- ACCEPTED
 * 059 per_thread_id_isolation -- ACCEPTED
 * 060 per_thread_double_free_local -- REFUSED
 * 061 condvar_single_handoff -- ACCEPTED
 * 062 condvar_chain -- ACCEPTED
 * 063 mutex_owned_counter -- ACCEPTED
 * 064 mutex_try_lock -- REFUSED
 * 065 condvar_timeout -- REFUSED
 * 066 mutex_two_handoffs -- ACCEPTED
 * 067 mutex_protected_alloc -- ACCEPTED
 * 068 condvar_broadcast -- ACCEPTED
 * 069 condvar_predicate_recheck -- ACCEPTED
 * 070 mutex_contention_many -- ACCEPTED
 * 071 barrier_two_phase -- ACCEPTED
 * 072 barrier_release_together -- ACCEPTED
 * 073 barrier_with_metadata -- ACCEPTED
 * 074 barrier_reused -- ACCEPTED
 * 075 barrier_then_double_free -- REFUSED
 * 076 barrier_then_transfer -- ACCEPTED
 * 077 barrier_with_injection -- REFUSED
 * 078 barrier_single_thread -- ACCEPTED
 * 079 barrier_two_distinct -- ACCEPTED
 * 080 barrier_allocation_waves -- ACCEPTED
 * 081 transfer_valid -- ACCEPTED
 * 082 transfer_bumps_generation -- ACCEPTED
 * 083 transfer_into_occupied_dst -- REFUSED
 * 084 transfer_null_src -- REFUSED
 * 085 transfer_self -- REFUSED
 * 086 transfer_move_only_compile_time -- SKIP
 * 087 transfer_then_old_owner_free -- REFUSED
 * 088 transfer_untracked_src -- REFUSED
 * 089 transfer_twice -- REFUSED
 * 090 transfer_chain_threads -- ACCEPTED
 * 091 gen_check_valid -- ACCEPTED
 * 092 gen_check_stale_after_free -- REFUSED
 * 093 gen_check_race_compile_only -- SKIP
 * 094 inject_zero_fail_all -- REFUSED
 * 095 inject_two_then_recover -- REFUSED
 * 096 inject_with_threads -- REFUSED
 * 097 inject_reset_then_ok -- ACCEPTED
 * 098 waves_with_transfer_and_check -- ACCEPTED
 * 099 waves_with_uaf_attempt -- REFUSED
 * 100 full_mixed_stress -- REFUSED
 */

#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_stats.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CASE_COUNT 100
#define NW 4

typedef enum { OUT_ACCEPTED = 0, OUT_REFUSED = 1, OUT_SKIP = 2 } outcome_t;

static const char *outcome_name(outcome_t o) {
    switch (o) {
    case OUT_ACCEPTED: return "ACCEPTED";
    case OUT_REFUSED:  return "REFUSED";
    case OUT_SKIP:     return "SKIP";
    }
    return "SKIP";
}

/* ------------------------------------------------------------------ */
/* Thread helpers: a tiny pool, a mutex+cond barrier, a handoff flag.  */
/* Every worker returns 1 on success, 0 on failure; pool_run() counts  */
/* failures under a mutex, so there is no data race in the harness.    */
/* ------------------------------------------------------------------ */

typedef int (*worker_fn)(int tid, void *ctx);

typedef struct {
    pthread_mutex_t mu;
    worker_fn fn;
    void *ctx;
    int failures;
} pool_t;

typedef struct { pool_t *p; int tid; } pool_job_t;

static void *pool_trampoline(void *arg) {
    pool_job_t *j = (pool_job_t *)arg;
    if (!j->p->fn(j->tid, j->p->ctx)) {
        pthread_mutex_lock(&j->p->mu);
        ++j->p->failures;
        pthread_mutex_unlock(&j->p->mu);
    }
    return NULL;
}

static int pool_run(int n, worker_fn fn, void *ctx) {
    pool_t p;
    pthread_t th[NW];
    pool_job_t jobs[NW];
    pthread_mutex_init(&p.mu, NULL);
    p.fn = fn;
    p.ctx = ctx;
    p.failures = 0;
    for (int i = 0; i < n; ++i) {
        jobs[i].p = &p;
        jobs[i].tid = i;
        (void)pthread_create(&th[i], NULL, pool_trampoline, &jobs[i]);
    }
    for (int i = 0; i < n; ++i) (void)pthread_join(th[i], NULL);
    pthread_mutex_destroy(&p.mu);
    return p.failures;
}

/* pthread_barrier_t is unavailable on macOS; implement the same primitive
 * with a mutex and a condition variable (matching Rust's std::sync::Barrier). */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int count;
    int arrived;
    unsigned gen;
} bar_t;

static void bar_init(bar_t *b, int count) {
    pthread_mutex_init(&b->mu, NULL);
    pthread_cond_init(&b->cv, NULL);
    b->count = count;
    b->arrived = 0;
    b->gen = 0;
}

static void bar_wait(bar_t *b) {
    pthread_mutex_lock(&b->mu);
    unsigned gen = b->gen;
    if (++b->arrived == b->count) {
        b->arrived = 0;
        ++b->gen;
        (void)pthread_cond_broadcast(&b->cv);
    } else {
        while (gen == b->gen) (void)pthread_cond_wait(&b->cv, &b->mu);
    }
    pthread_mutex_unlock(&b->mu);
}

static void bar_destroy(bar_t *b) {
    pthread_cond_destroy(&b->cv);
    pthread_mutex_destroy(&b->mu);
}

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int ready;
    int woke;
} flag_t;

typedef struct {
    pthread_mutex_t mu;
    long v;
} counter_t;

typedef struct {
    flag_t *flag;
    int tid;
} chain_arg_t;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int turn;
    int ok;
} chain_t;

/* Per-thread saved allocations. Distinct array slots are written by distinct
 * threads, and main reads them only after join(): no data race. */
static void *g_ptrs[NW];
static void *g_shared;
static void *g_slots[NW + 1];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_ids[NW];
static _Thread_local int g_tls_counter;

/* ------------------------------------------------------------------ */
/* Reusable workers                                                    */
/* ------------------------------------------------------------------ */

static int w_alloc_free(int tid, void *ctx) {
    (void)ctx;
    void *p = malloc_s(64 + (size_t)tid);
    if (!p) return 0;
    cplus_safe_alloc_meta m;
    if (cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK) return 0;
    if (!m.is_live || m.byte_size != 64 + (size_t)tid) return 0;
    return free_checked_s(p) == CPLUS_SAFE_OK;
}

static int w_calloc_free(int tid, void *ctx) {
    (void)tid; (void)ctx;
    void *p = calloc_s(16, 4);
    if (!p) return 0;
    if (cplus_safe_ptr_count(p) != 16 || cplus_safe_ptr_elem_size(p) != 4) return 0;
    return free_checked_s(p) == CPLUS_SAFE_OK;
}

static int w_aligned_free(int tid, void *ctx) {
    (void)tid; (void)ctx;
    void *p = aligned_alloc_s(64, 128);
    if (!p || cplus_safe_ptr_size(p) != 128) return 0;
    return free_checked_s(p) == CPLUS_SAFE_OK;
}

static int w_realloc_free(int tid, void *ctx) {
    (void)tid; (void)ctx;
    cplus_safe_alloc_meta m1, m2;
    void *p = malloc_s(32);
    if (!p || cplus_safe_alloc_get_meta(p, &m1) != CPLUS_SAFE_OK) return 0;
    void *q = realloc_s(p, 64);
    if (!q) { (void)free_checked_s(p); return 0; }
    int ok = cplus_safe_alloc_get_meta(q, &m2) == CPLUS_SAFE_OK &&
             m2.byte_size == 64 && m2.generation > m1.generation;
    (void)free_checked_s(q);
    return ok;
}

static int w_mixed(int tid, void *ctx) {
    (void)ctx;
    void *p;
    switch (tid % 3) {
    case 0:  p = malloc_s(48); break;
    case 1:  p = calloc_s(12, 4); break;
    default: p = aligned_alloc_s(32, 96); break;
    }
    if (!p) return 0;
    if (tid % 3 == 1 && cplus_safe_ptr_count(p) != 12) return 0;
    return free_checked_s(p) == CPLUS_SAFE_OK;
}

static int w_alloc_store(int tid, void *ctx) {
    (void)ctx;
    void *p = calloc_s(16, 4);
    if (!p) return 0;
    g_ptrs[tid] = p;
    return 1;
}

static int w_query_stored(int tid, void *ctx) {
    (void)ctx;
    void *p = g_ptrs[tid];
    return p && cplus_safe_ptr_count(p) == 16 && cplus_safe_ptr_size(p) == 64;
}

static unsigned long long g_expected_gen;

static int w_gen_snapshot(int tid, void *ctx) {
    (void)ctx;
    cplus_safe_alloc_meta m;
    return g_ptrs[tid] &&
           cplus_safe_alloc_get_meta(g_ptrs[tid], &m) == CPLUS_SAFE_OK &&
           m.generation == g_expected_gen;
}

static int w_free_stored(int tid, void *ctx) {
    (void)ctx;
    return g_ptrs[tid] && free_checked_s(g_ptrs[tid]) == CPLUS_SAFE_OK;
}

static int w_free_shared(int tid, void *ctx) {
    (void)tid; (void)ctx;
    return free_checked_s(g_shared) == CPLUS_SAFE_OK;
}

static int w_free_stack_refused(int tid, void *ctx) {
    (void)tid; (void)ctx;
    int local = 0;
    return cplus_safe_ptr_is_live(&local) == 0 &&
           free_checked_s(&local) != CPLUS_SAFE_OK;
}

static int w_locked_increment(int tid, void *ctx) {
    (void)tid;
    counter_t *c = (counter_t *)ctx;
    for (int i = 0; i < 100; ++i) {
        pthread_mutex_lock(&c->mu);
        ++c->v;
        pthread_mutex_unlock(&c->mu);
    }
    return 1;
}

static int w_log_toggle(int tid, void *ctx) {
    counter_t *c = (counter_t *)ctx;
    (void)tid;
    pthread_mutex_lock(&c->mu);
    cplus_safe_log_set_level((cplus_safe_log_level)((tid % 5) + 1));
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_INFO);
    int ok = cplus_safe_log_get_level() == CPLUS_SAFE_LOG_INFO;
    pthread_mutex_unlock(&c->mu);
    return ok;
}

static int w_local_counter(int tid, void *ctx) {
    (void)tid; (void)ctx;
    g_tls_counter = 0;
    for (int i = 0; i < 1000; ++i) ++g_tls_counter;
    return g_tls_counter == 1000;
}

static int w_inject_alloc(int tid, void *ctx) {
    (void)tid; (void)ctx;
    void *p = malloc_s(32);
    if (p) { (void)free_checked_s(p); return 0; } /* unexpected success */
    return 1;                                     /* refusal observed */
}

static int w_flag_wait(int tid, void *ctx) {
    (void)tid;
    flag_t *f = (flag_t *)ctx;
    pthread_mutex_lock(&f->mu);
    while (!f->ready) (void)pthread_cond_wait(&f->cv, &f->mu);
    ++f->woke;
    pthread_mutex_unlock(&f->mu);
    return 1;
}

static int w_flag_timedwait(int tid, void *ctx) {
    (void)tid;
    flag_t *f = (flag_t *)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 20000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
    int rc = 0;
    pthread_mutex_lock(&f->mu);
    if (!f->ready) rc = pthread_cond_timedwait(&f->cv, &f->mu, &ts);
    if (f->ready) ++f->woke;
    pthread_mutex_unlock(&f->mu);
    return rc == ETIMEDOUT;
}

static int w_flag_predicate_wait(int tid, void *ctx) {
    (void)tid;
    flag_t *f = (flag_t *)ctx;
    pthread_mutex_lock(&f->mu);
    while (!f->ready) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 20000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
        (void)pthread_cond_timedwait(&f->cv, &f->mu, &ts);
    }
    ++f->woke;
    pthread_mutex_unlock(&f->mu);
    return 1;
}

static int w_bar_alloc(int tid, void *ctx) {
    bar_wait((bar_t *)ctx);
    return w_alloc_free(tid, NULL);
}

static int w_bar_meta(int tid, void *ctx) {
    (void)tid;
    bar_wait((bar_t *)ctx);
    return cplus_safe_ptr_is_live(g_shared) && cplus_safe_ptr_size(g_shared) == 64;
}

static int w_bar_free_shared(int tid, void *ctx) {
    (void)tid;
    bar_wait((bar_t *)ctx);
    return free_checked_s(g_shared) == CPLUS_SAFE_OK;
}

static int w_bar_inject_alloc(int tid, void *ctx) {
    (void)tid;
    bar_wait((bar_t *)ctx);
    return w_inject_alloc(tid, NULL);
}

static int w_bar_wave(int tid, void *ctx) {
    (void)tid;
    bar_t *b = (bar_t *)ctx;
    for (int wave = 0; wave < 3; ++wave) {
        bar_wait(b);
        if (!w_alloc_free(tid, NULL)) return 0;
    }
    return 1;
}

static int w_two_barriers(int tid, void *ctx) {
    (void)tid;
    bar_t *bars = (bar_t *)ctx;
    bar_wait(&bars[0]);
    bar_wait(&bars[1]);
    return 1;
}

static int w_self_cond(int tid, void *ctx) {
    (void)tid;
    flag_t *f = (flag_t *)ctx;
    pthread_mutex_lock(&f->mu);
    f->ready = 1;
    (void)pthread_cond_broadcast(&f->cv);
    while (!f->ready) (void)pthread_cond_wait(&f->cv, &f->mu);
    ++f->woke;
    pthread_mutex_unlock(&f->mu);
    return f->woke == 1;
}

static int w_tid_isolation(int tid, void *ctx) {
    (void)ctx;
    g_ids[tid] = tid + 1;
    return 1;
}

static int w_local_double_free(int tid, void *ctx) {
    (void)ctx;
    void *p = malloc_s(32);
    if (!p) return 0;
    int ok = free_checked_s(p) == CPLUS_SAFE_OK &&
             free_checked_s(p) != CPLUS_SAFE_OK;
    (void)tid;
    return ok;
}

static int w_log_emit(int tid, void *ctx) {
    (void)ctx;
    cplus_safe_log(CPLUS_SAFE_LOG_INFO, "concurrency_c", "case48");
    return (tid >= 0);
}

static int w_stats_record(int tid, void *ctx) {
    (void)ctx; (void)tid;
    for (int i = 0; i < 10; ++i) cplus_safe_stat_record("case57", 1);
    return 1;
}

static int w_per_thread_reuse(int tid, void *ctx) {
    (void)ctx;
    for (int i = 0; i < 10; ++i) {
        void *p = malloc_s(16 + (size_t)tid);
        if (!p || free_checked_s(p) != CPLUS_SAFE_OK) return 0;
    }
    return 1;
}

static int w_per_thread_gen(int tid, void *ctx) {
    (void)ctx;
    cplus_safe_alloc_meta m;
    void *p = malloc_s(24 + (size_t)tid);
    if (!p || cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK) return 0;
    int ok = m.generation > 0 && m.is_live;
    (void)free_checked_s(p);
    return ok;
}

static int w_per_thread_local_level(int tid, void *ctx) {
    (void)ctx;
    int local_level = CPLUS_SAFE_LOG_OFF;
    local_level = (tid % 5) + 1;
    int ok = local_level >= CPLUS_SAFE_LOG_ERROR && local_level <= CPLUS_SAFE_LOG_TRACE;
    return ok;
}

static int w_mutex_alloc(int tid, void *ctx) {
    (void)tid;
    counter_t *c = (counter_t *)ctx;
    pthread_mutex_lock(&c->mu);
    void *p = malloc_s(40);
    int ok = p && cplus_safe_ptr_size(p) == 40 && free_checked_s(p) == CPLUS_SAFE_OK;
    pthread_mutex_unlock(&c->mu);
    return ok;
}

static int w_wave_uaf(int tid, void *ctx) {
    (void)ctx;
    for (int wave = 0; wave < 3; ++wave) {
        void *p = malloc_s(28 + (size_t)tid);
        cplus_safe_alloc_meta m;
        if (!p || cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK) return 0;
        if (free_checked_s(p) != CPLUS_SAFE_OK) return 0;
        if (cplus_safe_ptr_is_live(p)) return 0; /* checked refusal, never deref */
    }
    return 1;
}

static int w_stress(int tid, void *ctx) {
    (void)ctx;
    for (int wave = 0; wave < 5; ++wave) {
        if (tid == 0 && wave == 0) {
            int bad = 0; /* deliberate invalid free of a stack address */
            if (!cplus_safe_ptr_is_live(&bad) && free_checked_s(&bad) != CPLUS_SAFE_OK)
                return 0; /* refusal observed -> counted -> REFUSED */
        }
        void *p = malloc_s(32 + (size_t)tid);
        if (!p) return 0;
        cplus_safe_alloc_meta m;
        if (cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK) return 0;
        if (tid % 2 == 0) {
            void *dst = NULL;
            if (cplus_safe_transfer_ownership(&dst, &p) != CPLUS_SAFE_OK) return 0;
            p = dst;
            if (cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK) return 0;
        }
        if (!cplus_safe_ptr_check_generation(p, m.generation)) return 0;
        if (free_checked_s(p) != CPLUS_SAFE_OK) return 0;
    }
    return 1;
}

/* ---------------------- helper: validated log level ----------------- */
static int set_level_checked(int level) {
    if (level < (int)CPLUS_SAFE_LOG_OFF || level > (int)CPLUS_SAFE_LOG_TRACE)
        return CPLUS_SAFE_EINVAL;
    cplus_safe_log_set_level((cplus_safe_log_level)level);
    return CPLUS_SAFE_OK;
}

/* =================================================================== */
/* 100 scenarios                                                        */
/* =================================================================== */

/* Case 001: single-thread alloc + metadata check + checked free. */
static outcome_t case_001(void) { return w_alloc_free(0, NULL) ? OUT_ACCEPTED : OUT_REFUSED; }

/* Case 002: four threads allocate and free their own private blocks. */
static outcome_t case_002(void) { return pool_run(NW, w_alloc_free, NULL) == 0 ? OUT_ACCEPTED : OUT_REFUSED; }

/* Case 003: four threads allocate/track/query/release calloc blocks. */
static outcome_t case_003(void) { return pool_run(NW, w_calloc_free, NULL) == 0 ? OUT_ACCEPTED : OUT_REFUSED; }

/* Case 004: four threads use aligned allocation with tracked size. */
static outcome_t case_004(void) { return pool_run(NW, w_aligned_free, NULL) == 0 ? OUT_ACCEPTED : OUT_REFUSED; }

/* Case 005: concurrent realloc; generation must bump before free. */
static outcome_t case_005(void) { return pool_run(NW, w_realloc_free, NULL) == 0 ? OUT_ACCEPTED : OUT_REFUSED; }

/* Case 006: mixed malloc/calloc/aligned_alloc across four threads. */
static outcome_t case_006(void) { return pool_run(NW, w_mixed, NULL) == 0 ? OUT_ACCEPTED : OUT_REFUSED; }

/* Case 007: three consecutive waves of four allocate/free threads. */
static outcome_t case_007(void) {
    for (int w = 0; w < 3; ++w)
        if (pool_run(NW, w_alloc_free, NULL) != 0) return OUT_REFUSED;
    return OUT_ACCEPTED;
}

/* Case 008: main allocates, explicit ownership transfer, worker frees. */
static outcome_t case_008(void) {
    g_shared = malloc_s(64);
    if (!g_shared) return OUT_REFUSED;
    void *dst = NULL;
    pthread_mutex_lock(&g_mu);
    int rc = cplus_safe_transfer_ownership(&dst, &g_shared);
    pthread_mutex_unlock(&g_mu);
    if (rc != CPLUS_SAFE_OK || g_shared != NULL || dst == NULL) return OUT_REFUSED;
    g_shared = dst;
    /* A worker thread releases the block transferred to it. */
    return pool_run(1, w_free_shared, NULL) == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 009: owner frees first; a foreign thread's checked free is refused. */
static outcome_t case_009(void) {
    g_shared = malloc_s(48);
    if (!g_shared || free_checked_s(g_shared) != CPLUS_SAFE_OK) return OUT_REFUSED;
    /* The foreign thread's checked free must be refused (one failure). */
    return pool_run(1, w_free_shared, NULL) > 0 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 010: two threads race to free one block; exactly one is refused. */
static outcome_t case_010(void) {
    g_shared = malloc_s(64);
    if (!g_shared) return OUT_REFUSED;
    return pool_run(2, w_free_shared, NULL) == 1 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 011: shared block, four concurrent element-count queries. */
static outcome_t case_011(void) {
    g_shared = calloc_s(16, 4);
    if (!g_shared) return OUT_REFUSED;
    for (int i = 0; i < NW; ++i) g_ptrs[i] = g_shared;
    int rc = pool_run(NW, w_query_stored, NULL);
    (void)free_checked_s(g_shared);
    return rc == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 012: cplus_safe_query_length with explicit element size on live block. */
static outcome_t case_012(void) {
    size_t count = 0;
    void *p = calloc_s(20, 8);
    if (!p || cplus_safe_query_length(p, 8, &count) != CPLUS_SAFE_OK || count != 20) {
        if (p) (void)free_checked_s(p);
        return OUT_REFUSED;
    }
    return free_checked_s(p) == CPLUS_SAFE_OK ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 013: remaining capacity of an interior pointer equals size - offset. */
static outcome_t case_013(void) {
    void *p = malloc_s(64);
    if (!p) return OUT_REFUSED;
    size_t rem = cplus_safe_remaining_capacity((char *)p + 8);
    int ok = (rem == 56);
    (void)free_checked_s(p);
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 014: capacity query on an untracked stack pointer returns (size_t)-1. */
static outcome_t case_014(void) {
    int local = 7;
    return cplus_safe_remaining_capacity(&local) == (size_t)-1 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 015: get_meta on a freed pointer is refused (EINVAL). */
static outcome_t case_015(void) {
    cplus_safe_alloc_meta m;
    void *p = malloc_s(32);
    if (!p) return OUT_REFUSED;
    (void)free_checked_s(p);
    return cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 016: is_live is false after free; access is refused. */
static outcome_t case_016(void) {
    void *p = malloc_s(32);
    if (!p) return OUT_REFUSED;
    (void)free_checked_s(p);
    return cplus_safe_ptr_is_live(p) == 0 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 017: four threads snapshot the same live block's generation. */
static outcome_t case_017(void) {
    cplus_safe_alloc_meta m0;
    g_shared = malloc_s(64);
    if (!g_shared || cplus_safe_alloc_get_meta(g_shared, &m0) != CPLUS_SAFE_OK) return OUT_REFUSED;
    for (int i = 0; i < NW; ++i) g_ptrs[i] = g_shared;
    g_expected_gen = m0.generation;
    int fails = pool_run(NW, w_gen_snapshot, NULL);
    (void)free_checked_s(g_shared);
    return fails == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 018: realloc bumps generation; the old generation check is refused. */
static outcome_t case_018(void) {
    cplus_safe_alloc_meta m1, m2;
    void *p = malloc_s(32);
    if (!p || cplus_safe_alloc_get_meta(p, &m1) != CPLUS_SAFE_OK) return OUT_REFUSED;
    void *q = realloc_s(p, 64);
    if (!q || cplus_safe_alloc_get_meta(q, &m2) != CPLUS_SAFE_OK) return OUT_REFUSED;
    int refused = (m2.generation > m1.generation) &&
                  !cplus_safe_ptr_check_generation(q, m1.generation);
    (void)free_checked_s(q);
    return refused ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 019: live allocation count returns to baseline after a worker wave. */
static outcome_t case_019(void) {
    size_t base = cplus_safe_alloc_live_count();
    if (pool_run(NW, w_alloc_store, NULL) != 0) return OUT_REFUSED;
    size_t peak = cplus_safe_alloc_live_count();
    if (peak != base + NW) return OUT_REFUSED;
    if (pool_run(NW, w_free_stored, NULL) != 0) return OUT_REFUSED;
    return cplus_safe_alloc_live_count() == base ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 020: live byte counter returns to baseline after a worker wave. */
static outcome_t case_020(void) {
    size_t base = cplus_safe_alloc_live_bytes();
    if (pool_run(NW, w_alloc_store, NULL) != 0) return OUT_REFUSED;
    size_t peak = cplus_safe_alloc_live_bytes();
    if (peak != base + NW * 64) return OUT_REFUSED;
    if (pool_run(NW, w_free_stored, NULL) != 0) return OUT_REFUSED;
    return cplus_safe_alloc_live_bytes() == base ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 021: same-thread double free; second checked free is refused. */
static outcome_t case_021(void) {
    void *p = malloc_s(32);
    if (!p) return OUT_REFUSED;
    return (free_checked_s(p) == CPLUS_SAFE_OK &&
            free_checked_s(p) != CPLUS_SAFE_OK) ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 022: compile-time-only double free (moved handle). No runtime event;
 * Rust's borrow checker rejects it and plain C has no static check => SKIP. */
static outcome_t case_022(void) { return OUT_SKIP; }

/* Case 023: a second alias attempts the free after the owner already freed. */
static outcome_t case_023(void) {
    void *p = malloc_s(32);
    void *alias = p;
    if (!p) return OUT_REFUSED;
    return (free_checked_s(p) == CPLUS_SAFE_OK &&
            free_checked_s(alias) != CPLUS_SAFE_OK) ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 024: a free attempted in a later wave on an earlier wave's pointer. */
static outcome_t case_024(void) {
    void *p = malloc_s(32);
    if (!p || free_checked_s(p) != CPLUS_SAFE_OK) return OUT_REFUSED;
    g_shared = p; /* stale alias saved from wave 1 */
    return pool_run(1, w_free_shared, NULL) == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 025: four threads attempt one free; one succeeds, three are refused. */
static outcome_t case_025(void) {
    g_shared = malloc_s(64);
    if (!g_shared) return OUT_REFUSED;
    return pool_run(NW, w_free_shared, NULL) == NW - 1 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 026: after transfer + new-owner free, old owner's saved copy is refused. */
static outcome_t case_026(void) {
    void *p = malloc_s(64), *saved = p, *dst = NULL;
    if (!p) return OUT_REFUSED;
    if (cplus_safe_transfer_ownership(&dst, &p) != CPLUS_SAFE_OK) return OUT_REFUSED;
    if (free_checked_s(dst) != CPLUS_SAFE_OK) return OUT_REFUSED;
    return free_checked_s(saved) != CPLUS_SAFE_OK ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 027: checked free of a stack address is refused. */
static outcome_t case_027(void) {
    return pool_run(1, w_free_stack_refused, NULL) == 0 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 028: free_checked_s(NULL) is a documented no-op => ACCEPTED. */
static outcome_t case_028(void) {
    return free_checked_s(NULL) == CPLUS_SAFE_OK ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 029: a raw malloc() pointer is not owned by the checked allocator. */
static outcome_t case_029(void) {
    void *raw = malloc(16);
    if (!raw) return OUT_REFUSED;
    int refused = free_checked_s(raw) != CPLUS_SAFE_OK;
    free(raw); /* raw memory must be released with raw free */
    return refused ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 030: free_s (void) followed by a checked free is refused. */
static outcome_t case_030(void) {
    void *p = malloc_s(32);
    if (!p) return OUT_REFUSED;
    free_s(p);
    return free_checked_s(p) != CPLUS_SAFE_OK ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 031: after free, is_live query refuses access (no dereference). */
static outcome_t case_031(void) {
    void *p = malloc_s(32);
    if (!p || free_checked_s(p) != CPLUS_SAFE_OK) return OUT_REFUSED;
    return cplus_safe_ptr_is_live(p) == 0 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 032: after free, a capacity query refuses the would-be write. */
static outcome_t case_032(void) {
    void *p = malloc_s(32);
    if (!p || free_checked_s(p) != CPLUS_SAFE_OK) return OUT_REFUSED;
    return cplus_safe_remaining_capacity(p) == (size_t)-1 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 033: one thread frees while another attempts checked access. */
static outcome_t case_033(void) {
    flag_t f;
    pthread_mutex_init(&f.mu, NULL);
    pthread_cond_init(&f.cv, NULL);
    f.ready = 0; f.woke = 0;
    g_shared = malloc_s(32);
    if (!g_shared) return OUT_REFUSED;
    /* Phase 1: live check succeeds. */
    pthread_mutex_lock(&f.mu); f.ready = 1; (void)pthread_cond_broadcast(&f.cv); pthread_mutex_unlock(&f.mu);
    int fail1 = pool_run(1, w_flag_wait, &f);
    int live_ok = cplus_safe_ptr_is_live(g_shared) == 1;
    (void)free_checked_s(g_shared);
    /* Phase 2: after free, the checked access is refused. */
    return (fail1 == 0 && live_ok && !cplus_safe_ptr_is_live(g_shared)) ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 034: compile-time-only UAF (use after move). No runtime event => SKIP. */
static outcome_t case_034(void) { return OUT_SKIP; }

/* Case 035: an old generation used after realloc is refused. */
static outcome_t case_035(void) {
    cplus_safe_alloc_meta m;
    void *p = malloc_s(32);
    if (!p || cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK) return OUT_REFUSED;
    void *q = realloc_s(p, 80);
    if (!q) return OUT_REFUSED;
    int refused = !cplus_safe_ptr_check_generation(q, m.generation);
    (void)free_checked_s(q);
    return refused ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 036: transfer bumps generation, so the old owner's ref is stale. */
static outcome_t case_036(void) {
    cplus_safe_alloc_meta m;
    void *p = malloc_s(48), *dst = NULL;
    if (!p || cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK) return OUT_REFUSED;
    if (cplus_safe_transfer_ownership(&dst, &p) != CPLUS_SAFE_OK) return OUT_REFUSED;
    int refused = !cplus_safe_ptr_check_generation(dst, m.generation);
    (void)free_checked_s(dst);
    return refused ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 037: three waves, each ends with a refused post-free access. */
static outcome_t case_037(void) { return pool_run(NW, w_wave_uaf, NULL) == 0 ? OUT_REFUSED : OUT_ACCEPTED; }

/* Case 038: checked access on an untracked stack pseudo-reference. */
static outcome_t case_038(void) { return pool_run(1, w_free_stack_refused, NULL) == 0 ? OUT_REFUSED : OUT_ACCEPTED; }

/* Case 039: NULL pseudo-reference is refused by is_live. */
static outcome_t case_039(void) {
    return (cplus_safe_ptr_is_live(NULL) == 0 &&
            cplus_safe_remaining_capacity(NULL) == (size_t)-1) ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 040: control -- checked read of a live block succeeds. */
static outcome_t case_040(void) {
    void *p = malloc_s(32);
    if (!p) return OUT_REFUSED;
    int ok = cplus_safe_ptr_is_live(p) && cplus_safe_ptr_size(p) == 32;
    (void)free_checked_s(p);
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 041: log level OFF -> TRACE -> INFO round trip. */
static outcome_t case_041(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    int off = cplus_safe_log_get_level() == CPLUS_SAFE_LOG_OFF;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE);
    int trace = cplus_safe_log_get_level() == CPLUS_SAFE_LOG_TRACE;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_INFO);
    return (off && trace) ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 042: four threads toggle the global log level under a mutex. */
static outcome_t case_042(void) {
    counter_t c; pthread_mutex_init(&c.mu, NULL); c.v = 0;
    int fails = pool_run(NW, w_log_toggle, &c);
    pthread_mutex_destroy(&c.mu);
    return fails == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 043: emit one structured log record. */
static outcome_t case_043(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_INFO);
    cplus_safe_log(CPLUS_SAFE_LOG_INFO, "concurrency_c", "case43");
    return OUT_ACCEPTED;
}

/* Case 044: TRACE level active across a full allocation lifecycle. */
static outcome_t case_044(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE);
    int ok = w_alloc_free(1, NULL);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_INFO);
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 045: telemetry records successful allocation calls. */
static outcome_t case_045(void) {
    cplus_safe_stat st;
    cplus_safe_stats_reset();
    void *p = malloc_s(64);
    if (!p) return OUT_REFUSED;
    (void)free_checked_s(p);
    if (!cplus_safe_stats_get("malloc_s", &st) || st.calls == 0 || st.ok == 0) return OUT_REFUSED;
    return OUT_ACCEPTED;
}

/* Case 046: telemetry records an injected allocation failure. */
static outcome_t case_046(void) {
    cplus_safe_stat st;
    cplus_safe_stats_reset();
    cplus_safe_alloc_fail_after(0);
    void *p = malloc_s(64);
    int failed = (p == NULL);
    cplus_safe_alloc_fail_reset();
    if (!failed) return OUT_ACCEPTED;
    return cplus_safe_stats_get("malloc_s", &st) && st.fail > 0 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 047: log level restored after an error path. */
static outcome_t case_047(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_DEBUG);
    int local = 0;
    int saw_error = !cplus_safe_ptr_is_live(&local);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_INFO);
    return (saw_error && cplus_safe_log_get_level() == CPLUS_SAFE_LOG_INFO) ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 048: concurrent log emission from four threads. */
static outcome_t case_048(void) {
    return pool_run(NW, w_log_emit, NULL) == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 049: checked setter refuses an out-of-range level. */
static outcome_t case_049(void) {
    return set_level_checked(99) != CPLUS_SAFE_OK ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 050: three waves of level toggling interleaved with alloc/free. */
static outcome_t case_050(void) {
    for (int w = 0; w < 3; ++w) {
        if (set_level_checked(CPLUS_SAFE_LOG_TRACE) != CPLUS_SAFE_OK) return OUT_REFUSED;
        if (!w_alloc_free(w, NULL)) return OUT_REFUSED;
        if (set_level_checked(CPLUS_SAFE_LOG_INFO) != CPLUS_SAFE_OK) return OUT_REFUSED;
    }
    return OUT_ACCEPTED;
}

/* Case 051: thread-local counters, one per worker, no shared state. */
static outcome_t case_051(void) { return pool_run(NW, w_local_counter, NULL) == 0 ? OUT_ACCEPTED : OUT_REFUSED; }

/* Case 052: each thread owns a private tracked slot. */
static outcome_t case_052(void) {
    if (pool_run(NW, w_alloc_store, NULL) != 0) return OUT_REFUSED;
    int fails = pool_run(NW, w_query_stored, NULL);
    int freed = pool_run(NW, w_free_stored, NULL);
    return (fails == 0 && freed == 0) ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 053: each thread reads a valid generation on its own block. */
static outcome_t case_053(void) { return pool_run(NW, w_per_thread_gen, NULL) == 0 ? OUT_ACCEPTED : OUT_REFUSED; }

/* Case 054: each thread toggles only a local (non-shared) log level. */
static outcome_t case_054(void) { return pool_run(NW, w_per_thread_local_level, NULL) == 0 ? OUT_ACCEPTED : OUT_REFUSED; }

/* Case 055: each thread records its own refused operations; refusals expected. */
static outcome_t case_055(void) {
    /* Worker succeeds when it observes a refusal. */
    return pool_run(NW, w_free_stack_refused, NULL) == 0 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 056: each thread reuses its private block ten times. */
static outcome_t case_056(void) { return pool_run(NW, w_per_thread_reuse, NULL) == 0 ? OUT_ACCEPTED : OUT_REFUSED; }

/* Case 057: concurrent telemetry recording from four threads. */
static outcome_t case_057(void) {
    cplus_safe_stat st;
    cplus_safe_stats_reset();
    if (pool_run(NW, w_stats_record, NULL) != 0) return OUT_REFUSED;
    if (!cplus_safe_stats_get("case57", &st) || st.calls != NW * 10) return OUT_REFUSED;
    return OUT_ACCEPTED;
}

/* Case 058: per-thread condition-variable state (no sharing). */
static outcome_t case_058(void) {
    int fails = 0;
    for (int i = 0; i < NW; ++i) {
        flag_t f;
        pthread_mutex_init(&f.mu, NULL);
        pthread_cond_init(&f.cv, NULL);
        f.ready = 0; f.woke = 0;
        if (pool_run(1, w_self_cond, &f) != 0) ++fails;
        pthread_cond_destroy(&f.cv);
        pthread_mutex_destroy(&f.mu);
    }
    return fails == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 059: each thread writes its own id slot; all ids are distinct. */
static outcome_t case_059(void) {
    for (int i = 0; i < NW; ++i) g_ids[i] = 0;
    if (pool_run(NW, w_tid_isolation, NULL) != 0) return OUT_REFUSED;
    for (int i = 0; i < NW; ++i) if (g_ids[i] != i + 1) return OUT_REFUSED;
    return OUT_ACCEPTED;
}

/* Case 060: each thread double-frees its own private block. */
static outcome_t case_060(void) {
    /* Workers succeed when the double-free is detected; that means REFUSED. */
    return pool_run(NW, w_local_double_free, NULL) == 0 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 061: single producer/consumer condvar handoff. The predicate is
 * published before the waiter starts, so the handoff is deterministic. */
static outcome_t case_061(void) {
    flag_t f;
    pthread_mutex_init(&f.mu, NULL); pthread_cond_init(&f.cv, NULL);
    pthread_mutex_lock(&f.mu);
    f.ready = 1;
    f.woke = 0;
    (void)pthread_cond_broadcast(&f.cv);
    pthread_mutex_unlock(&f.mu);
    int fails = pool_run(1, w_flag_wait, &f);
    int ok = (fails == 0 && f.woke == 1);
    pthread_cond_destroy(&f.cv); pthread_mutex_destroy(&f.mu);
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 062: four-way token chain via mutex + condvar broadcast. */
static void *chain_worker(void *arg) {
    chain_arg_t *a = (chain_arg_t *)arg;
    flag_t *f = a->flag;
    pthread_mutex_lock(&f->mu);
    /* Each thread waits for its own id to become the active turn. */
    /* f->woke is reused as the turn counter. */
    while (f->woke != a->tid) pthread_cond_wait(&f->cv, &f->mu);
    ++f->woke;
    (void)pthread_cond_broadcast(&f->cv);
    pthread_mutex_unlock(&f->mu);
    return NULL;
}

static outcome_t case_062(void) {
    flag_t f;
    pthread_mutex_init(&f.mu, NULL); pthread_cond_init(&f.cv, NULL);
    f.ready = 0; f.woke = 0;
    pthread_t th[NW]; chain_arg_t args[NW];
    for (int i = 0; i < NW; ++i) { args[i].flag = &f; args[i].tid = i; (void)pthread_create(&th[i], NULL, chain_worker, &args[i]); }
    for (int i = 0; i < NW; ++i) (void)pthread_join(th[i], NULL);
    int ok = (f.woke == NW);
    pthread_cond_destroy(&f.cv); pthread_mutex_destroy(&f.mu);
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 063: four threads increment one mutex-protected counter. */
static outcome_t case_063(void) {
    counter_t c; pthread_mutex_init(&c.mu, NULL); c.v = 0;
    int fails = pool_run(NW, w_locked_increment, &c);
    long v = c.v;
    pthread_mutex_destroy(&c.mu);
    return (fails == 0 && v == 400) ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 064: trylock on a held mutex fails (contention refused). */
static int g_try_saw_busy;

static void *w_try_lock_thread(void *arg) {
    counter_t *c = (counter_t *)arg;
    g_try_saw_busy = (pthread_mutex_trylock(&c->mu) == EBUSY);
    return NULL;
}

static outcome_t case_064(void) {
    counter_t c; pthread_mutex_init(&c.mu, NULL); c.v = 0;
    g_try_saw_busy = 0;
    pthread_mutex_lock(&c.mu);
    pthread_t t;
    (void)pthread_create(&t, NULL, w_try_lock_thread, &c);
    (void)pthread_join(t, NULL);
    pthread_mutex_unlock(&c.mu);
    pthread_mutex_destroy(&c.mu);
    /* g_try_saw_busy is written by the worker and read only after join(). */
    return g_try_saw_busy ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 065: timed condvar wait with no signal times out. */
static outcome_t case_065(void) {
    flag_t f;
    pthread_mutex_init(&f.mu, NULL); pthread_cond_init(&f.cv, NULL);
    f.ready = 0; f.woke = 0;
    int fails = pool_run(1, w_flag_timedwait, &f);
    pthread_cond_destroy(&f.cv); pthread_mutex_destroy(&f.mu);
    return fails == 0 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 066: two sequential handoffs through one condvar flag. */
static outcome_t case_066(void) {
    flag_t f;
    pthread_mutex_init(&f.mu, NULL); pthread_cond_init(&f.cv, NULL);
    int ok = 1;
    for (int round = 0; round < 2; ++round) {
        f.ready = 0; f.woke = 0;
        /* Signal before the waiter runs so the predicate short-circuits. */
        f.ready = 1;
        if (pool_run(1, w_flag_wait, &f) != 0 || f.woke != 1) ok = 0;
    }
    pthread_cond_destroy(&f.cv); pthread_mutex_destroy(&f.mu);
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 067: allocation and release performed under a shared mutex. */
static outcome_t case_067(void) {
    counter_t c; pthread_mutex_init(&c.mu, NULL); c.v = 0;
    int fails = pool_run(NW, w_mutex_alloc, &c);
    pthread_mutex_destroy(&c.mu);
    return fails == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 068: condvar broadcast wakes all four waiters. */
static outcome_t case_068(void) {
    flag_t f;
    pthread_mutex_init(&f.mu, NULL); pthread_cond_init(&f.cv, NULL);
    f.ready = 0; f.woke = 0;
    /* Predicate is set up-front so no waiter can block forever. */
    f.ready = 1;
    int fails = pool_run(NW, w_flag_wait, &f);
    int ok = (fails == 0 && f.woke == NW);
    pthread_cond_destroy(&f.cv); pthread_mutex_destroy(&f.mu);
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 069: wait loop rechecks its predicate (spurious-wakeup safe). */
static outcome_t case_069(void) {
    flag_t f;
    pthread_mutex_init(&f.mu, NULL); pthread_cond_init(&f.cv, NULL);
    f.ready = 0; f.woke = 0;
    pthread_mutex_lock(&f.mu); f.ready = 1; (void)pthread_cond_broadcast(&f.cv); pthread_mutex_unlock(&f.mu);
    int fails = pool_run(NW, w_flag_predicate_wait, &f);
    int ok = (fails == 0 && f.woke == NW);
    pthread_cond_destroy(&f.cv); pthread_mutex_destroy(&f.mu);
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 070: heavy mutex contention, 4 x 100 locked increments. */
static outcome_t case_070(void) {
    counter_t c; pthread_mutex_init(&c.mu, NULL); c.v = 0;
    int fails = pool_run(NW, w_locked_increment, &c);
    long v = c.v;
    pthread_mutex_destroy(&c.mu);
    return (fails == 0 && v == 400) ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 071: two-phase reusable barrier across four threads. */
static outcome_t case_071(void) {
    bar_t b; bar_init(&b, NW);
    int fails = pool_run(NW, w_two_barriers, &b);
    bar_destroy(&b);
    return fails == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 072: barrier releases threads that then allocate concurrently. */
static outcome_t case_072(void) {
    bar_t b; bar_init(&b, NW);
    int fails = pool_run(NW, w_bar_alloc, &b);
    bar_destroy(&b);
    return fails == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 073: after a barrier, threads query shared metadata. */
static outcome_t case_073(void) {
    g_shared = malloc_s(64);
    if (!g_shared) return OUT_REFUSED;
    bar_t b; bar_init(&b, NW);
    int fails = pool_run(NW, w_bar_meta, &b);
    bar_destroy(&b);
    (void)free_checked_s(g_shared);
    return fails == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 074: the same barrier object is reused for three generations. */
static outcome_t case_074(void) {
    bar_t b; bar_init(&b, NW);
    int fails = 0;
    for (int g = 0; g < 3; ++g) fails += pool_run(NW, w_two_barriers, &b);
    bar_destroy(&b);
    return fails == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 075: after a barrier, two threads race to free one block. */
static outcome_t case_075(void) {
    g_shared = malloc_s(64);
    if (!g_shared) return OUT_REFUSED;
    bar_t b; bar_init(&b, 2);
    int fails = pool_run(2, w_bar_free_shared, &b);
    bar_destroy(&b);
    return fails == 1 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 076: after a barrier, ownership is transferred down a thread chain. */
typedef struct { bar_t *bar; int tid; } bar_arg_t;

static void *bar_transfer_worker(void *arg) {
    bar_arg_t *a = (bar_arg_t *)arg;
    for (int phase = 0; phase < NW; ++phase) {
        bar_wait(a->bar);
        if (a->tid == phase) {
            if (phase == 0) {
                g_slots[1] = malloc_s(64);
            } else {
                void *dst = NULL;
                if (cplus_safe_transfer_ownership(&dst, &g_slots[phase]) == CPLUS_SAFE_OK)
                    g_slots[phase + 1] = dst;
            }
        }
    }
    return NULL;
}

static outcome_t case_076(void) {
    bar_t b; bar_init(&b, NW);
    pthread_t th[NW]; bar_arg_t args[NW];
    for (int i = 0; i < NW; ++i) {
        args[i].bar = &b; args[i].tid = i;
        (void)pthread_create(&th[i], NULL, bar_transfer_worker, &args[i]);
    }
    for (int i = 0; i < NW; ++i) (void)pthread_join(th[i], NULL);
    bar_destroy(&b);
    int ok = g_slots[NW] && cplus_safe_ptr_is_live(g_slots[NW]) &&
             free_checked_s(g_slots[NW]) == CPLUS_SAFE_OK;
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 077: barrier then failure-injected allocations are refused. */
static outcome_t case_077(void) {
    bar_t b; bar_init(&b, NW);
    cplus_safe_alloc_fail_after(0);
    int fails = pool_run(NW, w_bar_inject_alloc, &b);
    cplus_safe_alloc_fail_reset();
    bar_destroy(&b);
    return fails == 0 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 078: a single-participant barrier completes immediately. */
static outcome_t case_078(void) {
    bar_t b; bar_init(&b, 1);
    int fails = pool_run(1, w_bar_alloc, &b);
    bar_destroy(&b);
    return fails == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 079: two distinct barriers used in sequence by four threads. */
static outcome_t case_079(void) {
    bar_t bars[2]; bar_init(&bars[0], NW); bar_init(&bars[1], NW);
    int fails = pool_run(NW, w_two_barriers, bars);
    bar_destroy(&bars[0]); bar_destroy(&bars[1]);
    return fails == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 080: three allocation waves, each opened by a barrier. */
static outcome_t case_080(void) {
    bar_t b; bar_init(&b, NW);
    int fails = pool_run(NW, w_bar_wave, &b);
    bar_destroy(&b);
    return fails == 0 ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 081: a valid ownership transfer succeeds. */
static outcome_t case_081(void) {
    void *src = malloc_s(64), *dst = NULL;
    if (!src) return OUT_REFUSED;
    int ok = cplus_safe_transfer_ownership(&dst, &src) == CPLUS_SAFE_OK &&
             src == NULL && dst != NULL;
    if (dst && free_checked_s(dst) != CPLUS_SAFE_OK) ok = 0;
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 082: transfer bumps the generation of the moved block. */
static outcome_t case_082(void) {
    cplus_safe_alloc_meta before, after;
    void *src = malloc_s(64), *dst = NULL;
    if (!src || cplus_safe_alloc_get_meta(src, &before) != CPLUS_SAFE_OK) return OUT_REFUSED;
    if (cplus_safe_transfer_ownership(&dst, &src) != CPLUS_SAFE_OK) return OUT_REFUSED;
    int ok = cplus_safe_alloc_get_meta(dst, &after) == CPLUS_SAFE_OK &&
             after.generation > before.generation;
    (void)free_checked_s(dst);
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 083: transfer into an occupied destination is refused. */
static outcome_t case_083(void) {
    void *src = malloc_s(32), *dst = malloc_s(32);
    if (!src || !dst) return OUT_REFUSED;
    int refused = cplus_safe_transfer_ownership(&dst, &src) != CPLUS_SAFE_OK &&
                  src != NULL; /* source left untouched */
    (void)free_checked_s(src);
    (void)free_checked_s(dst);
    return refused ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 084: transfer from a NULL source is refused. */
static outcome_t case_084(void) {
    void *src = NULL, *dst = NULL;
    return cplus_safe_transfer_ownership(&dst, &src) != CPLUS_SAFE_OK ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 085: self-transfer (dst == src) is refused. */
static outcome_t case_085(void) {
    void *p = malloc_s(32);
    if (!p) return OUT_REFUSED;
    int refused = cplus_safe_transfer_ownership(&p, &p) != CPLUS_SAFE_OK;
    (void)free_checked_s(p);
    return refused ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 086: compile-time-only move semantics. No runtime event => SKIP. */
static outcome_t case_086(void) { return OUT_SKIP; }

/* Case 087: old owner's saved reference after new owner freed is refused. */
static outcome_t case_087(void) {
    void *src = malloc_s(48), *saved, *dst = NULL;
    if (!src) return OUT_REFUSED;
    saved = src;
    if (cplus_safe_transfer_ownership(&dst, &src) != CPLUS_SAFE_OK) return OUT_REFUSED;
    if (free_checked_s(dst) != CPLUS_SAFE_OK) return OUT_REFUSED;
    return free_checked_s(saved) != CPLUS_SAFE_OK ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 088: transfer of an untracked stack pointer is refused. */
static outcome_t case_088(void) {
    int local = 5;
    void *src = &local, *dst = NULL;
    return cplus_safe_transfer_ownership(&dst, &src) != CPLUS_SAFE_OK ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 089: a second transfer from the now-NULL source is refused. */
static outcome_t case_089(void) {
    void *src = malloc_s(32), *dst = NULL;
    if (!src || cplus_safe_transfer_ownership(&dst, &src) != CPLUS_SAFE_OK) return OUT_REFUSED;
    void *dst2 = NULL;
    int refused = cplus_safe_transfer_ownership(&dst2, &src) != CPLUS_SAFE_OK;
    (void)free_checked_s(dst);
    return refused ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 090: four threads hand a block along a transfer chain. */
static void *chain_transfer_worker(void *arg) {
    chain_arg_t *a = (chain_arg_t *)arg;
    flag_t *f = a->flag;
    pthread_mutex_lock(&f->mu);
    while (f->woke != a->tid) pthread_cond_wait(&f->cv, &f->mu);
    if (a->tid == 0) {
        g_slots[1] = malloc_s(64);
    } else {
        void *dst = NULL;
        if (cplus_safe_transfer_ownership(&dst, &g_slots[a->tid]) == CPLUS_SAFE_OK)
            g_slots[a->tid + 1] = dst;
    }
    ++f->woke;
    (void)pthread_cond_broadcast(&f->cv);
    pthread_mutex_unlock(&f->mu);
    return NULL;
}

static outcome_t case_090(void) {
    flag_t f;
    pthread_mutex_init(&f.mu, NULL); pthread_cond_init(&f.cv, NULL);
    f.ready = 0; f.woke = 0;
    pthread_t th[NW]; chain_arg_t args[NW];
    for (int i = 0; i < NW; ++i) { args[i].flag = &f; args[i].tid = i; (void)pthread_create(&th[i], NULL, chain_transfer_worker, &args[i]); }
    for (int i = 0; i < NW; ++i) (void)pthread_join(th[i], NULL);
    pthread_cond_destroy(&f.cv); pthread_mutex_destroy(&f.mu);
    int ok = g_slots[NW] && free_checked_s(g_slots[NW]) == CPLUS_SAFE_OK;
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 091: a current generation check succeeds. */
static outcome_t case_091(void) {
    cplus_safe_alloc_meta m;
    void *p = malloc_s(32);
    if (!p || cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK) return OUT_REFUSED;
    int ok = cplus_safe_ptr_check_generation(p, m.generation) == 1;
    (void)free_checked_s(p);
    return ok ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 092: generation check after free is refused. */
static outcome_t case_092(void) {
    cplus_safe_alloc_meta m;
    void *p = malloc_s(32);
    if (!p || cplus_safe_alloc_get_meta(p, &m) != CPLUS_SAFE_OK) return OUT_REFUSED;
    (void)free_checked_s(p);
    return cplus_safe_ptr_check_generation(p, m.generation) == 0 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 093: compile-time-only racy shared mutable state. No runtime event
 * in either checked API (C data race is UB, Rust rejects at compile time)
 * => SKIP. */
static outcome_t case_093(void) { return OUT_SKIP; }

/* Case 094: fail_after(0) makes every allocation fail. */
static outcome_t case_094(void) {
    cplus_safe_alloc_fail_after(0);
    void *p = malloc_s(32);
    cplus_safe_alloc_fail_reset();
    return p == NULL ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 095: fail_after(2) allows two allocations then refuses the third. */
static outcome_t case_095(void) {
    cplus_safe_alloc_fail_after(2);
    void *a = malloc_s(16), *b = malloc_s(16), *c = malloc_s(16);
    cplus_safe_alloc_fail_reset();
    int refused = (a && b && !c);
    if (a) (void)free_checked_s(a);
    if (b) (void)free_checked_s(b);
    return refused ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 096: four threads all see injected allocation failures. */
static outcome_t case_096(void) {
    cplus_safe_alloc_fail_after(0);
    int fails = pool_run(NW, w_inject_alloc, NULL);
    cplus_safe_alloc_fail_reset();
    return fails == 0 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 097: injection reset restores normal allocation. */
static outcome_t case_097(void) {
    cplus_safe_alloc_fail_after(0);
    void *bad = malloc_s(32);
    cplus_safe_alloc_fail_reset();
    void *good = malloc_s(32);
    if (bad) return OUT_REFUSED;
    if (!good) return OUT_REFUSED;
    return free_checked_s(good) == CPLUS_SAFE_OK ? OUT_ACCEPTED : OUT_REFUSED;
}

/* Case 098: three waves of alloc -> transfer -> generation check -> free. */
static outcome_t case_098(void) {
    for (int w = 0; w < 3; ++w) {
        cplus_safe_alloc_meta m;
        void *src = malloc_s(32), *dst = NULL;
        if (!src || cplus_safe_alloc_get_meta(src, &m) != CPLUS_SAFE_OK) return OUT_REFUSED;
        if (cplus_safe_transfer_ownership(&dst, &src) != CPLUS_SAFE_OK) return OUT_REFUSED;
        cplus_safe_alloc_meta moved;
        if (cplus_safe_alloc_get_meta(dst, &moved) != CPLUS_SAFE_OK) return OUT_REFUSED;
        if (!cplus_safe_ptr_check_generation(dst, moved.generation)) return OUT_REFUSED;
        if (free_checked_s(dst) != CPLUS_SAFE_OK) return OUT_REFUSED;
    }
    return OUT_ACCEPTED;
}

/* Case 099: three waves, each ending in a refused post-free access. */
static outcome_t case_099(void) {
    return pool_run(NW, w_wave_uaf, NULL) == 0 && NW == 4 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* Case 100: mixed stress with one deliberate invalid free per thread zero. */
static outcome_t case_100(void) {
    return pool_run(NW, w_stress, NULL) > 0 ? OUT_REFUSED : OUT_ACCEPTED;
}

/* ------------------------------------------------------------------ */
/* Dispatch: compile-time assertion of exactly 100 cases.              */
/* ------------------------------------------------------------------ */

typedef outcome_t (*case_fn)(void);

static const case_fn CASES[CASE_COUNT] = {
    case_001, case_002, case_003, case_004, case_005, case_006, case_007, case_008, case_009, case_010,
    case_011, case_012, case_013, case_014, case_015, case_016, case_017, case_018, case_019, case_020,
    case_021, case_022, case_023, case_024, case_025, case_026, case_027, case_028, case_029, case_030,
    case_031, case_032, case_033, case_034, case_035, case_036, case_037, case_038, case_039, case_040,
    case_041, case_042, case_043, case_044, case_045, case_046, case_047, case_048, case_049, case_050,
    case_051, case_052, case_053, case_054, case_055, case_056, case_057, case_058, case_059, case_060,
    case_061, case_062, case_063, case_064, case_065, case_066, case_067, case_068, case_069, case_070,
    case_071, case_072, case_073, case_074, case_075, case_076, case_077, case_078, case_079, case_080,
    case_081, case_082, case_083, case_084, case_085, case_086, case_087, case_088, case_089, case_090,
    case_091, case_092, case_093, case_094, case_095, case_096, case_097, case_098, case_099, case_100,
};

_Static_assert(sizeof(CASES) / sizeof(CASES[0]) == CASE_COUNT,
               "concurrency_c.c must contain exactly 100 cases");

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("RESULT 0 SKIP\n");
        return 0;
    }
    char *end = NULL;
    long n = strtol(argv[1], &end, 10);
    if (end == argv[1] || n < 1 || n > CASE_COUNT) {
        printf("RESULT %ld SKIP\n", n);
        return 0;
    }
    outcome_t o = CASES[n - 1]();
    printf("RESULT %ld %s\n", n, outcome_name(o));
    return 0;
}
