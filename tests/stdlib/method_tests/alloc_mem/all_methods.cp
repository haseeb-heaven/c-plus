/* 100% public-method invocation suite for:
 *   stdlib/alloc_policy.hp, stdlib/error.hp, stdlib/allocator.hp, stdlib/memory.hp
 *
 * Every public static-inline function and every public class method declared in
 * those four headers is invoked at least once with valid, bounded inputs and
 * its observed result is asserted.  One line is printed per method:
 *     <header> <method> PASS|FAIL
 * Final line: METHOD_TEST alloc_mem <total> <passed> <failed> <skipped>
 * Exit status is nonzero iff at least one method FAILs.
 *
 * Determinism: no wall-clock, no randomness, no unbounded sizes.  All heap
 * blocks come from stdlib_alloc/stdlib_calloc and are released.  No stale
 * pointer is ever dereferenced; stale tokens are only passed to guarded calls
 * that refuse them before touching memory.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_policy.hp"
#include "stdlib/allocator.hp"
#include "stdlib/memory.hp"

static int mt_total = 0;
static int mt_passed = 0;
static int mt_failed = 0;

/* Callback observation globals (deterministic, no addresses printed). */
static int mt_cb_calls = 0;
static int mt_cb_cat = -1;
static int mt_cb_code = -1;

static void mt_cb(int category, cplus_exit_code code, const char *message) {
    mt_cb_calls++;
    mt_cb_cat = (int)category;
    mt_cb_code = (int)code;
    if (message == (const char *)0) {
        mt_cb_code = -2;
    }
}

static void mt_report(const char *hdr, const char *mth, int ok) {
    mt_total++;
    if (ok)
        mt_passed++;
    else
        mt_failed++;
    print(hdr, mth, ok ? "PASS" : "FAIL");
}

/* Read one byte through memcpy so strict mode never sees a pointer subscript
 * on an unverified pointer parameter. */
static unsigned char mt_byte(const void *p, size_t i) {
    unsigned char b = 0;
    __builtin_memcpy(&b, (const unsigned char *)p + i, 1);
    return b;
}

static int mt_all_zero(const void *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (mt_byte(p, i) != (unsigned char)0)
            return 0;
    return 1;
}

static char mt_meta_buf[8];

/* ======================================================================= */
/* stdlib/alloc_policy.hp                                                    */
/* ======================================================================= */

static void t01_count_size_overflow(void) {
    size_t r_total = 9;
    int ok;
    ok = (stdlib_count_size_overflow(6, 7, &r_total) == 0 && r_total == 42);
    ok = ok && (stdlib_count_size_overflow(1, 99, &r_total) == 0 && r_total == 99);
    ok = ok && (stdlib_count_size_overflow(5, 0, &r_total) == 0 && r_total == 0);
    r_total = 9;
    ok = ok && (stdlib_count_size_overflow(SIZE_MAX, 2, &r_total) == 1 &&
                r_total == SIZE_MAX);
    ok = ok && (stdlib_count_size_overflow(3, 4, (size_t *)0) == 1);
    mt_report("alloc_policy.hp", "stdlib_count_size_overflow", ok);
}

static void t02_size_class_round_up(void) {
    size_t r_out = 777;
    int ok;
    ok = (stdlib_size_class_round_up(13, 8, &r_out) == 0 && r_out == 16);
    ok = ok && (stdlib_size_class_round_up(16, 8, &r_out) == 0 && r_out == 16);
    r_out = 777;
    ok = ok && (stdlib_size_class_round_up(10, 0, &r_out) == -1 && r_out == 777);
    ok = ok && (stdlib_size_class_round_up(10, 3, &r_out) == -1 && r_out == 777);
    ok = ok && (stdlib_size_class_round_up(SIZE_MAX, 8, &r_out) == -1 && r_out == 777);
    ok = ok && (stdlib_size_class_round_up(10, 8, (size_t *)0) == -1);
    mt_report("alloc_policy.hp", "stdlib_size_class_round_up", ok);
}

static void t03_size_class_words(void) {
    size_t r_words = 777;
    const size_t w = sizeof(uintptr_t);
    int ok;
    ok = (stdlib_size_class_words(0, &r_words) == 0 && r_words == 0);
    ok = ok && (stdlib_size_class_words(1, &r_words) == 0 && r_words == 1);
    ok = ok && (stdlib_size_class_words(w, &r_words) == 0 && r_words == 1);
    ok = ok && (stdlib_size_class_words(w + 1, &r_words) == 0 && r_words == 2);
    r_words = 777;
    ok = ok && (stdlib_size_class_words(SIZE_MAX, &r_words) == -1 && r_words == 777);
    ok = ok && (stdlib_size_class_words(8, (size_t *)0) == -1);
    mt_report("alloc_policy.hp", "stdlib_size_class_words", ok);
}

static void t04_size_normalize_zero(void) {
    size_t r_out = 777;
    int ok;
    ok = (stdlib_size_normalize_zero(0, 8, &r_out) == 0 && r_out == 8);
    ok = ok && (stdlib_size_normalize_zero(0, 1, &r_out) == 0 && r_out == 1);
    ok = ok && (stdlib_size_normalize_zero(42, 8, &r_out) == 0 && r_out == 42);
    ok = ok && (stdlib_size_normalize_zero(42, 1, (size_t *)0) == -1);
    mt_report("alloc_policy.hp", "stdlib_size_normalize_zero", ok);
}

/* ======================================================================= */
/* stdlib/error.hp : telemetry + reporting                                   */
/* ======================================================================= */

static void t05_tm_add(void) {
    stdlib_telemetry_stats e_snap;
    int ok;
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_tm_add(STDLIB_TM_ALLOC, 5);
    stdlib_tm_add(STDLIB_TM_INVALID, 2);
    stdlib_telemetry_snapshot(&e_snap);
    ok = (e_snap.alloc_count == 5 && e_snap.invalid_ops == 2);
    stdlib_telemetry_set_enabled(0);
    stdlib_tm_add(STDLIB_TM_ALLOC, 100);
    stdlib_telemetry_snapshot(&e_snap);
    ok = ok && (e_snap.alloc_count == 5);
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    mt_report("error.hp", "stdlib_tm_add", ok);
}

static void t06_telemetry_set_enabled(void) {
    int ok;
    stdlib_telemetry_set_enabled(1);
    ok = (stdlib_telemetry_is_enabled() == 1);
    stdlib_telemetry_set_enabled(0);
    ok = ok && (stdlib_telemetry_is_enabled() == 0);
    stdlib_telemetry_set_enabled(1);
    mt_report("error.hp", "stdlib_telemetry_set_enabled", ok);
}

static void t07_telemetry_is_enabled(void) {
    stdlib_telemetry_set_enabled(1);
    mt_report("error.hp", "stdlib_telemetry_is_enabled", stdlib_telemetry_is_enabled() == 1);
}

static void t08_telemetry_reset(void) {
    stdlib_telemetry_stats e_snap;
    int ok;
    stdlib_telemetry_set_enabled(1);
    stdlib_tm_add(STDLIB_TM_ALLOC, 3);
    stdlib_tm_add(STDLIB_TM_API_FAIL, 4);
    stdlib_telemetry_reset();
    stdlib_telemetry_snapshot(&e_snap);
    ok = (e_snap.alloc_count == 0 && e_snap.api_failures == 0 &&
          e_snap.free_count == 0 && e_snap.bytes_allocated == 0);
    mt_report("error.hp", "stdlib_telemetry_reset", ok);
}

static void t09_telemetry_snapshot(void) {
    void *e_p;
    stdlib_telemetry_stats e_snap;
    int ok;
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    e_p = stdlib_alloc(13);
    stdlib_telemetry_snapshot(&e_snap);
    ok = (e_snap.alloc_count == 1 && e_snap.bytes_allocated == 13);
    stdlib_telemetry_snapshot((stdlib_telemetry_stats *)0); /* NULL is a no-op */
    if (e_p != (void *)0)
        stdlib_release(e_p);
    mt_report("error.hp", "stdlib_telemetry_snapshot", ok);
}

static void t10_set_error_callback(void) {
    int ok;
    mt_cb_calls = 0;
    mt_cb_cat = -1;
    mt_cb_code = -1;
    stdlib_set_error_callback(mt_cb);
    stdlib_note(STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_OWNERSHIP, "cb-test");
    ok = (mt_cb_calls == 1 && mt_cb_cat == STDLIB_CATEGORY_OWNERSHIP &&
          mt_cb_code == (int)CPLUS_EXIT_OWNERSHIP);
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_note(STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS, "cb-off");
    ok = ok && (mt_cb_calls == 1);
    mt_report("error.hp", "stdlib_set_error_callback", ok);
}

static void t11_category_name(void) {
    int ok;
    ok = (strcmp(stdlib_category_name(STDLIB_CATEGORY_OK), "ok") == 0);
    ok = ok && (strcmp(stdlib_category_name(STDLIB_CATEGORY_INVALID), "invalid") == 0);
    ok = ok && (strcmp(stdlib_category_name(STDLIB_CATEGORY_BOUNDS), "bounds") == 0);
    ok = ok && (strcmp(stdlib_category_name(STDLIB_CATEGORY_OWNERSHIP), "ownership") == 0);
    ok = ok && (strcmp(stdlib_category_name(STDLIB_CATEGORY_LIFECYCLE), "lifecycle") == 0);
    ok = ok && (strcmp(stdlib_category_name(STDLIB_CATEGORY_UTF8), "utf8") == 0);
    ok = ok && (strcmp(stdlib_category_name(999), "unknown") == 0);
    mt_report("error.hp", "stdlib_category_name", ok);
}

static void t12_note(void) {
    stdlib_telemetry_stats e_snap;
    int ok;
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    mt_cb_calls = 0;
    stdlib_set_error_callback(mt_cb);
    stdlib_note(STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE, "note-test");
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_snapshot(&e_snap);
    ok = (e_snap.alloc_failures == 1 && e_snap.api_failures == 1);
    ok = ok && (mt_cb_calls == 1 && mt_cb_cat == STDLIB_CATEGORY_MEMORY &&
                mt_cb_code == (int)CPLUS_EXIT_RESOURCE);
    mt_report("error.hp", "stdlib_note", ok);
}

/* stdlib/error.hp : allocation-failure injection -------------------------- */

static void t13_alloc_fail_at(void) {
    void *e_a;
    void *e_b;
    int ok;
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_at(2);
    ok = (stdlib_alloc_seq() == 0);
    e_a = stdlib_alloc(8);
    ok = ok && (e_a != (void *)0 && stdlib_alloc_seq() == 1);
    e_b = stdlib_alloc(8);
    ok = ok && (e_b == (void *)0 && stdlib_alloc_seq() == 2);
    if (e_a != (void *)0)
        stdlib_release(e_a);
    stdlib_alloc_fail_reset();
    mt_report("error.hp", "stdlib_alloc_fail_at", ok);
}

static void t14_alloc_fail_after(void) {
    void *e_a;
    void *e_b;
    int ok;
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_after(1);
    e_a = stdlib_alloc(8);
    ok = (e_a != (void *)0);
    e_b = stdlib_alloc(8);
    ok = ok && (e_b == (void *)0 && stdlib_alloc_seq() == 2);
    if (e_a != (void *)0)
        stdlib_release(e_a);
    stdlib_alloc_fail_reset();
    mt_report("error.hp", "stdlib_alloc_fail_after", ok);
}

static void t15_alloc_fail_reset(void) {
    void *e_p;
    int ok;
    stdlib_alloc_fail_at(1);
    stdlib_alloc_fail_reset();
    ok = (stdlib_alloc_seq() == 0);
    e_p = stdlib_alloc(8);
    ok = ok && (e_p != (void *)0);
    if (e_p != (void *)0)
        stdlib_release(e_p);
    mt_report("error.hp", "stdlib_alloc_fail_reset", ok);
}

static void t16_alloc_seq(void) {
    void *e_a;
    void *e_b;
    int ok;
    stdlib_alloc_fail_reset();
    ok = (stdlib_alloc_seq() == 0);
    e_a = stdlib_alloc(8);
    e_b = stdlib_alloc(8);
    ok = ok && (stdlib_alloc_seq() == 2);
    stdlib_alloc_fail_reset();
    ok = ok && (stdlib_alloc_seq() == 0);
    if (e_a != (void *)0)
        stdlib_release(e_a);
    if (e_b != (void *)0)
        stdlib_release(e_b);
    mt_report("error.hp", "stdlib_alloc_seq", ok);
}

/* stdlib/error.hp : metadata side table ----------------------------------- */

static void t17_meta_acquire(void) {
    int ok;
    ok = (stdlib_state.meta_lock == 0);
    stdlib_meta_acquire();
    ok = ok && (stdlib_state.meta_lock == 1);
    stdlib_meta_release_lock();
    mt_report("error.hp", "stdlib_meta_acquire", ok);
}

static void t18_meta_release_lock(void) {
    int ok;
    stdlib_meta_acquire();
    ok = (stdlib_state.meta_lock == 1);
    stdlib_meta_release_lock();
    ok = ok && (stdlib_state.meta_lock == 0);
    mt_report("error.hp", "stdlib_meta_release_lock", ok);
}

static void t19_meta_hash(void) {
    void *e_p = stdlib_alloc(8);
    const void *e_q = e_p;
    size_t e_h0 = stdlib_meta_hash((const void *)0);
    size_t e_h1;
    size_t e_h2;
    int ok;
    ok = (e_h0 == (size_t)0);
    if (e_p != (void *)0) {
        e_h1 = stdlib_meta_hash(e_p);
        e_h2 = stdlib_meta_hash(e_q);
        ok = ok && (e_h1 == e_h2);
        stdlib_release(e_p);
    } else {
        ok = 0;
    }
    mt_report("error.hp", "stdlib_meta_hash", ok);
}

static void t20_meta_find(void) {
    void *e_p = stdlib_alloc(16);
    stdlib_meta_slot *e_s;
    int e_local = 7;
    int ok;
    if (e_p == (void *)0) {
        mt_report("error.hp", "stdlib_meta_find", 0);
        return;
    }
    e_s = stdlib_meta_find(e_p);
    ok = (e_s != (stdlib_meta_slot *)0 && e_s->ptr == e_p &&
          e_s->byte_size == 16 && e_s->elem_size == 1 && e_s->generation > 0);
    ok = ok && (stdlib_meta_find((const void *)0) == (stdlib_meta_slot *)0);
    ok = ok && (stdlib_meta_find(&e_local) == (stdlib_meta_slot *)0);
    stdlib_release(e_p);
    mt_report("error.hp", "stdlib_meta_find", ok);
}

static void t21_meta_record(void) {
    stdlib_meta_slot *e_s;
    int ok;
    stdlib_meta_record(mt_meta_buf, 24, 3);
    e_s = stdlib_meta_find(mt_meta_buf);
    ok = (e_s != (stdlib_meta_slot *)0 && e_s->byte_size == 24 &&
          e_s->elem_size == 3 && e_s->generation > 0);
    stdlib_meta_forget(mt_meta_buf);
    mt_report("error.hp", "stdlib_meta_record", ok);
}

static void t22_meta_forget(void) {
    int ok;
    stdlib_meta_record(mt_meta_buf, 10, 2);
    ok = (stdlib_meta_find(mt_meta_buf) != (stdlib_meta_slot *)0);
    stdlib_meta_forget(mt_meta_buf);
    ok = ok && (stdlib_meta_find(mt_meta_buf) == (stdlib_meta_slot *)0);
    stdlib_meta_forget((const void *)0); /* NULL is a no-op */
    mt_report("error.hp", "stdlib_meta_forget", ok);
}

/* stdlib/error.hp : checked allocator ------------------------------------- */

static void t23_alloc(void) {
    void *e_p = stdlib_alloc(24);
    void *e_z = stdlib_alloc(0);
    void *e_big = stdlib_alloc(STDLIB_ALLOC_LIMIT + 1);
    int ok;
    ok = (e_p != (void *)0 && stdlib_alloc_size(e_p) == 24 &&
          stdlib_alloc_elem_size(e_p) == 1);
    ok = ok && (e_z != (void *)0 && stdlib_alloc_size(e_z) == 1);
    ok = ok && (e_big == (void *)0);
    if (e_p != (void *)0)
        stdlib_release(e_p);
    if (e_z != (void *)0)
        stdlib_release(e_z);
    mt_report("error.hp", "stdlib_alloc", ok);
}

static void t24_calloc(void) {
    void *e_p = stdlib_calloc(8, 4);
    void *e_z = stdlib_calloc(0, 8);
    int ok;
    ok = (e_p != (void *)0 && stdlib_alloc_size(e_p) == 32 &&
          stdlib_alloc_elem_size(e_p) == 4 && stdlib_alloc_length(e_p) == 8 &&
          mt_all_zero(e_p, 32));
    ok = ok && (stdlib_calloc(SIZE_MAX, 2) == (void *)0);
    ok = ok && (e_z != (void *)0 && stdlib_alloc_size(e_z) == 1 &&
                stdlib_alloc_elem_size(e_z) == 8);
    if (e_p != (void *)0)
        stdlib_release(e_p);
    if (e_z != (void *)0)
        stdlib_release(e_z);
    mt_report("error.hp", "stdlib_calloc", ok);
}

static void t25_realloc(void) {
    void *e_p = stdlib_alloc(16);
    void *e_q;
    void *e_n;
    unsigned long long e_g0;
    unsigned long long e_g1;
    int ok;
    if (e_p == (void *)0) {
        mt_report("error.hp", "stdlib_realloc", 0);
        return;
    }
    e_g0 = stdlib_alloc_generation(e_p);
    e_q = stdlib_realloc(e_p, 48);
    e_g1 = stdlib_alloc_generation(e_q);
    ok = (e_q != (void *)0 && stdlib_alloc_size(e_q) == 48 && e_g1 > e_g0 &&
          stdlib_alloc_check_generation(e_q, e_g1) == 1 &&
          stdlib_alloc_check_generation(e_q, e_g0) == 0);
    ok = ok && (stdlib_realloc(e_q, STDLIB_ALLOC_LIMIT + 1) == (void *)0 &&
                stdlib_alloc_size(e_q) == 48 &&
                stdlib_alloc_generation(e_q) == e_g1);
    e_n = stdlib_realloc((void *)0, 20);
    ok = ok && (e_n != (void *)0 && stdlib_alloc_size(e_n) == 20);
    if (e_q != (void *)0)
        stdlib_release(e_q);
    if (e_n != (void *)0)
        stdlib_release(e_n);
    mt_report("error.hp", "stdlib_realloc", ok);
}

static void t26_safe_div(void) {
    int ok;
    ok = (stdlib_safe_div(100, 4) == 25);
    ok = ok && (stdlib_safe_div(0, 5) == 0);
    ok = ok && (stdlib_safe_div(100, 0) == 0);
    ok = ok && (stdlib_safe_div(7, 1) == 7);
    ok = ok && (stdlib_safe_div(1000000, 1000) == 1000);
    mt_report("error.hp", "stdlib_safe_div", ok);
}

static void t27_alloc_size(void) {
    void *e_p = stdlib_alloc(33);
    int e_local = 5;
    int ok;
    ok = (stdlib_alloc_size(e_p) == 33);
    ok = ok && (stdlib_alloc_size((const void *)0) == 0);
    ok = ok && (stdlib_alloc_size(&e_local) == 0);
    if (e_p != (void *)0)
        stdlib_release(e_p);
    mt_report("error.hp", "stdlib_alloc_size", ok);
}

static void t28_alloc_elem_size(void) {
    void *e_c = stdlib_calloc(4, 7);
    void *e_a = stdlib_alloc(9);
    int ok;
    ok = (stdlib_alloc_elem_size(e_c) == 7);
    ok = ok && (stdlib_alloc_elem_size(e_a) == 1);
    ok = ok && (stdlib_alloc_elem_size((const void *)0) == 0);
    if (e_c != (void *)0)
        stdlib_release(e_c);
    if (e_a != (void *)0)
        stdlib_release(e_a);
    mt_report("error.hp", "stdlib_alloc_elem_size", ok);
}

static void t29_alloc_length(void) {
    void *e_c = stdlib_calloc(5, 8);
    void *e_a = stdlib_alloc(7);
    int ok;
    ok = (stdlib_alloc_length(e_c) == 5);
    ok = ok && (stdlib_alloc_length(e_a) == 7);
    ok = ok && (stdlib_alloc_length((const void *)0) == 0);
    if (e_c != (void *)0)
        stdlib_release(e_c);
    if (e_a != (void *)0)
        stdlib_release(e_a);
    mt_report("error.hp", "stdlib_alloc_length", ok);
}

static void t30_alloc_generation(void) {
    void *e_a = stdlib_alloc(8);
    void *e_b = stdlib_alloc(8);
    unsigned long long e_ga;
    unsigned long long e_gb;
    int ok;
    e_ga = stdlib_alloc_generation(e_a);
    e_gb = stdlib_alloc_generation(e_b);
    ok = (e_ga > 0 && e_gb > e_ga);
    ok = ok && (stdlib_alloc_generation((const void *)0) == 0);
    if (e_a != (void *)0)
        stdlib_release(e_a);
    if (e_b != (void *)0)
        stdlib_release(e_b);
    mt_report("error.hp", "stdlib_alloc_generation", ok);
}

static void t31_alloc_check_generation(void) {
    void *e_p = stdlib_alloc(8);
    unsigned long long e_g;
    void *e_q;
    int ok;
    if (e_p == (void *)0) {
        mt_report("error.hp", "stdlib_alloc_check_generation", 0);
        return;
    }
    e_g = stdlib_alloc_generation(e_p);
    ok = (stdlib_alloc_check_generation(e_p, e_g) == 1);
    ok = ok && (stdlib_alloc_check_generation(e_p, e_g + 1) == 0);
    ok = ok && (stdlib_alloc_check_generation((const void *)0, e_g) == 0);
    ok = ok && (stdlib_alloc_check_generation(e_p, 0) == 0);
    e_q = stdlib_realloc(e_p, 16);
    ok = ok && (stdlib_alloc_check_generation(e_q, e_g) == 0);
    if (e_q != (void *)0)
        stdlib_release(e_q);
    mt_report("error.hp", "stdlib_alloc_check_generation", ok);
}

static void t32_alloc_query_length(void) {
    void *e_c = stdlib_calloc(6, 5);
    size_t e_n = 99;
    int ok;
    if (e_c == (void *)0) {
        mt_report("error.hp", "stdlib_alloc_query_length", 0);
        return;
    }
    ok = (stdlib_alloc_query_length(e_c, 0, &e_n) == 0 && e_n == 6);
    e_n = 99;
    ok = ok && (stdlib_alloc_query_length(e_c, 5, &e_n) == 0 && e_n == 6);
    e_n = 99;
    ok = ok && (stdlib_alloc_query_length(e_c, 10, &e_n) == 0 && e_n == 3);
    e_n = 99;
    ok = ok && (stdlib_alloc_query_length((const void *)0, 5, &e_n) == -1 && e_n == 0);
    ok = ok && (stdlib_alloc_query_length(e_c, 5, (size_t *)0) == -1);
    stdlib_release(e_c);
    mt_report("error.hp", "stdlib_alloc_query_length", ok);
}

static void t33_transfer_ownership(void) {
    void *e_src = stdlib_alloc(12);
    void *e_dst = (void *)0;
    void *e_other;
    void *e_empty = (void *)0;
    unsigned long long e_g0;
    int ok;
    if (e_src == (void *)0) {
        mt_report("error.hp", "stdlib_transfer_ownership", 0);
        return;
    }
    e_g0 = stdlib_alloc_generation(e_src);
    ok = (stdlib_transfer_ownership(&e_dst, &e_src) == 0);
    ok = ok && (e_dst != (void *)0 && e_src == (void *)0 &&
                stdlib_alloc_generation(e_dst) > e_g0);
    ok = ok && (stdlib_transfer_ownership((void **)0, &e_dst) == -1);
    e_other = e_dst;
    ok = ok && (stdlib_transfer_ownership(&e_other, &e_dst) == -1);
    ok = ok && (stdlib_transfer_ownership(&e_other, &e_empty) == -1); /* src empty */
    if (e_dst != (void *)0)
        stdlib_release(e_dst);
    mt_report("error.hp", "stdlib_transfer_ownership", ok);
}

static void t34_release(void) {
    void *e_p = stdlib_alloc(10);
    const void *e_old = e_p;
    unsigned long long e_g;
    int ok;
    if (e_p == (void *)0) {
        mt_report("error.hp", "stdlib_release", 0);
        return;
    }
    e_g = stdlib_alloc_generation(e_p);
    ok = (e_g > 0);
    stdlib_release(e_p);
    ok = ok && (stdlib_alloc_size(e_old) == 0 && stdlib_alloc_generation(e_old) == 0);
    stdlib_release((void *)0); /* no-op */
    mt_report("error.hp", "stdlib_release", ok);
}

/* stdlib/error.hp : error class methods ----------------------------------- */

static void t35_error_ctor(void) {
    error e_e = error();
    char e_c = 'x';
    int ok;
    __builtin_memcpy(&e_c, e_e.what(), 1);
    ok = (e_e.ok() == 1 && e_e.failed_now() == 0 &&
          e_e.code() == (int)CPLUS_EXIT_OK &&
          e_e.category() == STDLIB_CATEGORY_OK && e_c == '\0');
    mt_report("error.hp", "error::error", ok);
}

static void t36_error_ok(void) {
    error e_e = error();
    int ok;
    ok = (e_e.ok() == 1);
    e_e.set(CPLUS_EXIT_BOUNDS, "ok-test");
    ok = ok && (e_e.ok() == 0);
    e_e.clear();
    ok = ok && (e_e.ok() == 1);
    mt_report("error.hp", "error::ok", ok);
}

static void t37_error_failed_now(void) {
    error e_e = error();
    int ok;
    ok = (e_e.failed_now() == 0);
    e_e.set(CPLUS_EXIT_BOUNDS, "failed-test");
    ok = ok && (e_e.failed_now() == 1);
    e_e.clear();
    ok = ok && (e_e.failed_now() == 0);
    mt_report("error.hp", "error::failed_now", ok);
}

static void t38_error_code(void) {
    error e_e = error();
    int ok;
    e_e.set(CPLUS_EXIT_BOUNDS, "code-test");
    ok = (e_e.code() == (int)CPLUS_EXIT_BOUNDS);
    e_e.set(CPLUS_EXIT_RESOURCE, "code-test-2");
    ok = ok && (e_e.code() == (int)CPLUS_EXIT_RESOURCE);
    mt_report("error.hp", "error::code", ok);
}

static void t39_error_what(void) {
    error e_e = error();
    char e_c = 'x';
    int ok;
    e_e.set(CPLUS_EXIT_BOUNDS, "hello-what");
    ok = (strcmp(e_e.what(), "hello-what") == 0);
    e_e.clear();
    __builtin_memcpy(&e_c, e_e.what(), 1);
    ok = ok && (e_c == '\0');
    mt_report("error.hp", "error::what", ok);
}

static void t40_error_category(void) {
    error e_e = error();
    int ok;
    ok = (e_e.category() == STDLIB_CATEGORY_OK);
    e_e.set_category(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_TYPE_CONFUSION, "cat-test");
    ok = ok && (e_e.category() == STDLIB_CATEGORY_RANGE);
    mt_report("error.hp", "error::category", ok);
}

static void t41_error_clear(void) {
    error e_e = error();
    int ok;
    e_e.set_category(STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE, "clr");
    ok = (e_e.failed_now() == 1);
    e_e.clear();
    ok = ok && (e_e.ok() == 1 && e_e.failed_now() == 0 &&
                e_e.code() == (int)CPLUS_EXIT_OK &&
                e_e.category() == STDLIB_CATEGORY_OK);
    mt_report("error.hp", "error::clear", ok);
}

static void t42_error_set(void) {
    error e_e = error();
    int ok;
    e_e.set(CPLUS_EXIT_RESOURCE, "set-msg");
    ok = (e_e.failed_now() == 1 && e_e.code() == (int)CPLUS_EXIT_RESOURCE &&
          e_e.category() == STDLIB_CATEGORY_MEMORY &&
          strcmp(e_e.what(), "set-msg") == 0);
    mt_report("error.hp", "error::set", ok);
}

static void t43_error_set_category(void) {
    error e_e = error();
    int ok;
    e_e.set_category(STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE, "lc");
    ok = (e_e.category() == STDLIB_CATEGORY_LIFECYCLE &&
          e_e.code() == (int)CPLUS_EXIT_USE_AFTER_FREE &&
          strcmp(e_e.what(), "lc") == 0);
    mt_report("error.hp", "error::set_category", ok);
}

static void t44_error_reset(void) {
    error e_e = error();
    int ok;
    e_e.set(CPLUS_EXIT_BOUNDS, "reset-test");
    error_reset(&e_e);
    ok = (e_e.ok() == 1 && e_e.category() == STDLIB_CATEGORY_OK);
    error_reset((void *)0); /* no-op */
    mt_report("error.hp", "error_reset", ok);
}

static void t45_error_fail(void) {
    error e_e = error();
    int ok;
    error_fail(&e_e, CPLUS_EXIT_BOUNDS, "bf");
    ok = (e_e.category() == STDLIB_CATEGORY_BOUNDS &&
          e_e.code() == (int)CPLUS_EXIT_BOUNDS);
    error_reset(&e_e);
    error_fail(&e_e, CPLUS_EXIT_RESOURCE, "mf");
    ok = ok && (e_e.category() == STDLIB_CATEGORY_MEMORY);
    error_reset(&e_e);
    error_fail(&e_e, CPLUS_EXIT_USE_AFTER_FREE, "lf");
    ok = ok && (e_e.category() == STDLIB_CATEGORY_LIFECYCLE);
    error_reset(&e_e);
    error_fail(&e_e, CPLUS_EXIT_IO, "iof");
    ok = ok && (e_e.category() == STDLIB_CATEGORY_IO);
    error_fail((void *)0, CPLUS_EXIT_BOUNDS, "null-target");
    mt_report("error.hp", "error_fail", ok);
}

static void t46_error_fail_cat(void) {
    error e_e = error();
    int ok;
    error_fail_cat(&e_e, STDLIB_CATEGORY_UTF8, CPLUS_EXIT_TYPE_CONFUSION, "uc");
    ok = (e_e.category() == STDLIB_CATEGORY_UTF8 &&
          e_e.code() == (int)CPLUS_EXIT_TYPE_CONFUSION &&
          strcmp(e_e.what(), "uc") == 0);
    error_fail_cat(&e_e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_UNSUPPORTED, (const char *)0);
    ok = ok && (e_e.category() == STDLIB_CATEGORY_RANGE);
    error_fail_cat((void *)0, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_UNSUPPORTED, "nt");
    mt_report("error.hp", "error_fail_cat", ok);
}

/* ======================================================================= */
/* stdlib/allocator.hp                                                        */
/* ======================================================================= */

static void t47_alloc_identity(void) {
    void *al_p = stdlib_alloc(12);
    stdlib_alloc_id al_id;
    int ok;
    ok = (stdlib_alloc_identity(al_p, &al_id) == 0);
    ok = ok && (al_id.base == al_p && al_id.generation > 0 &&
                al_id.byte_size == 12 && al_id.elem_size == 1 &&
                al_id.generation == stdlib_alloc_generation(al_p));
    ok = ok && (stdlib_alloc_identity((const void *)0, &al_id) == -1 &&
                al_id.base == (const void *)0 && al_id.generation == 0);
    ok = ok && (stdlib_alloc_identity(al_p, (stdlib_alloc_id *)0) == -1);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    mt_report("allocator.hp", "stdlib_alloc_identity", ok);
}

static void t48_alloc_id_equal(void) {
    void *al_p = stdlib_alloc(8);
    void *al_q;
    stdlib_alloc_id al_a;
    stdlib_alloc_id al_b;
    stdlib_alloc_id al_zero;
    int ok;
    if (al_p == (void *)0) {
        mt_report("allocator.hp", "stdlib_alloc_id_equal", 0);
        return;
    }
    stdlib_alloc_identity(al_p, &al_a);
    stdlib_alloc_identity(al_p, &al_b);
    ok = (stdlib_alloc_id_equal(&al_a, &al_b) == 1);
    al_q = stdlib_realloc(al_p, 16);
    stdlib_alloc_identity(al_q, &al_b);
    ok = ok && (stdlib_alloc_id_equal(&al_a, &al_b) == 0);
    __builtin_memset(&al_zero, 0, sizeof(al_zero));
    ok = ok && (stdlib_alloc_id_equal(&al_zero, &al_zero) == 0);
    ok = ok && (stdlib_alloc_id_equal((const stdlib_alloc_id *)0, &al_a) == 0);
    ok = ok && (stdlib_alloc_id_equal(&al_a, (const stdlib_alloc_id *)0) == 0);
    if (al_q != (void *)0)
        stdlib_release(al_q);
    mt_report("allocator.hp", "stdlib_alloc_id_equal", ok);
}

static void t49_alloc_id_matches(void) {
    void *al_p = stdlib_alloc(8);
    void *al_q;
    stdlib_alloc_id al_id;
    stdlib_alloc_id al_zero;
    int ok;
    if (al_p == (void *)0) {
        mt_report("allocator.hp", "stdlib_alloc_id_matches", 0);
        return;
    }
    stdlib_alloc_identity(al_p, &al_id);
    ok = (stdlib_alloc_id_matches(&al_id, al_p) == 1);
    al_q = stdlib_realloc(al_p, 16);
    ok = ok && (stdlib_alloc_id_matches(&al_id, al_q) == 0);
    __builtin_memset(&al_zero, 0, sizeof(al_zero));
    ok = ok && (stdlib_alloc_id_matches(&al_zero, al_q) == 0);
    ok = ok && (stdlib_alloc_id_matches(&al_id, (const void *)0) == 0);
    ok = ok && (stdlib_alloc_id_matches((const stdlib_alloc_id *)0, al_q) == 0);
    if (al_q != (void *)0)
        stdlib_release(al_q);
    mt_report("allocator.hp", "stdlib_alloc_id_matches", ok);
}

static void t50_view_bytes(void) {
    void *al_p = stdlib_alloc(19);
    const void *al_inner = (const char *)al_p + 1;
    int ok;
    ok = (stdlib_alloc_view_bytes(al_p) == 19);
    ok = ok && (stdlib_alloc_view_bytes((const void *)0) == 0);
    ok = ok && (stdlib_alloc_view_bytes(al_inner) == 0);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    mt_report("allocator.hp", "stdlib_alloc_view_bytes", ok);
}

static void t51_view_elem_bytes(void) {
    void *al_c = stdlib_calloc(3, 6);
    void *al_a = stdlib_alloc(11);
    int ok;
    ok = (stdlib_alloc_view_elem_bytes(al_c) == 6);
    ok = ok && (stdlib_alloc_view_elem_bytes(al_a) == 1);
    ok = ok && (stdlib_alloc_view_elem_bytes((const void *)0) == 0);
    if (al_c != (void *)0)
        stdlib_release(al_c);
    if (al_a != (void *)0)
        stdlib_release(al_a);
    mt_report("allocator.hp", "stdlib_alloc_view_elem_bytes", ok);
}

static void t52_view_generation(void) {
    void *al_p = stdlib_alloc(8);
    int ok;
    ok = (stdlib_alloc_view_generation(al_p) > 0 &&
          stdlib_alloc_view_generation(al_p) == stdlib_alloc_generation(al_p));
    ok = ok && (stdlib_alloc_view_generation((const void *)0) == 0);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    mt_report("allocator.hp", "stdlib_alloc_view_generation", ok);
}

static void t53_view_live(void) {
    void *al_p = stdlib_alloc(8);
    const void *al_old = al_p;
    int ok;
    ok = (stdlib_alloc_view_live(al_p) == 1);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    ok = ok && (stdlib_alloc_view_live(al_old) == 0);
    ok = ok && (stdlib_alloc_view_live((const void *)0) == 0);
    mt_report("allocator.hp", "stdlib_alloc_view_live", ok);
}

static void t54_view_count(void) {
    void *al_c = stdlib_calloc(4, 8);
    void *al_a = stdlib_alloc(7);
    int ok;
    ok = (stdlib_alloc_view_count(al_c) == 4);
    ok = ok && (stdlib_alloc_view_count(al_a) == 7);
    ok = ok && (stdlib_alloc_view_count((const void *)0) == 0);
    if (al_c != (void *)0)
        stdlib_release(al_c);
    if (al_a != (void *)0)
        stdlib_release(al_a);
    mt_report("allocator.hp", "stdlib_alloc_view_count", ok);
}

static void t55_view_remaining(void) {
    void *al_p = stdlib_alloc(20);
    int ok;
    ok = (stdlib_alloc_view_remaining(al_p, 0) == 20);
    ok = ok && (stdlib_alloc_view_remaining(al_p, 5) == 15);
    ok = ok && (stdlib_alloc_view_remaining(al_p, 20) == 0);
    ok = ok && (stdlib_alloc_view_remaining(al_p, 21) == 0);
    ok = ok && (stdlib_alloc_view_remaining((const void *)0, 0) == 0);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    mt_report("allocator.hp", "stdlib_alloc_view_remaining", ok);
}

static void t56_view_unchanged(void) {
    void *al_p = stdlib_alloc(8);
    void *al_q;
    unsigned long long al_g;
    int ok;
    if (al_p == (void *)0) {
        mt_report("allocator.hp", "stdlib_alloc_view_unchanged", 0);
        return;
    }
    al_g = stdlib_alloc_generation(al_p);
    ok = (stdlib_alloc_view_unchanged(al_p, al_g) == 1);
    al_q = stdlib_realloc(al_p, 16);
    ok = ok && (stdlib_alloc_view_unchanged(al_q, al_g) == 0);
    ok = ok && (stdlib_alloc_view_unchanged(al_q, 0) == 0);
    ok = ok && (stdlib_alloc_view_unchanged((const void *)0, al_g) == 0);
    if (al_q != (void *)0)
        stdlib_release(al_q);
    mt_report("allocator.hp", "stdlib_alloc_view_unchanged", ok);
}

static void t57_view_transfer_ok(void) {
    void *al_src = stdlib_alloc(8);
    void *al_dst = (void *)0;
    void *al_h;
    int e_local = 3;
    void *al_untracked = &e_local;
    int ok;
    if (al_src == (void *)0) {
        mt_report("allocator.hp", "stdlib_alloc_view_transfer_ok", 0);
        return;
    }
    ok = (stdlib_alloc_view_transfer_ok(&al_dst, &al_src) == 1);
    al_dst = al_src;
    ok = ok && (stdlib_alloc_view_transfer_ok(&al_dst, &al_src) == 0);
    al_dst = (void *)0;
    ok = ok && (stdlib_alloc_view_transfer_ok(&al_dst, (void **)0) == 0);
    ok = ok && (stdlib_alloc_view_transfer_ok((void **)0, &al_src) == 0);
    al_h = &al_src;
    ok = ok && (stdlib_alloc_view_transfer_ok(&al_h, &al_src) == 0);
    ok = ok && (stdlib_alloc_view_transfer_ok(&al_dst, &al_untracked) == 0);
    stdlib_release(al_src);
    mt_report("allocator.hp", "stdlib_alloc_view_transfer_ok", ok);
}

static void t58_cap_state(void) {
    void *al_p = stdlib_alloc(8);
    const void *al_old = al_p;
    int ok;
    ok = (stdlib_alloc_cap_state(al_p) == STDLIB_ALLOC_CAP_LIVE);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    ok = ok && (stdlib_alloc_cap_state(al_old) == STDLIB_ALLOC_CAP_NONE);
    ok = ok && (stdlib_alloc_cap_state((const void *)0) == STDLIB_ALLOC_CAP_NONE);
    mt_report("allocator.hp", "stdlib_alloc_cap_state", ok);
}

static void t59_hasvalidcap(void) {
    void *al_p = stdlib_alloc(8);
    const void *al_old = al_p;
    int ok;
    ok = (stdlib_alloc_hasvalidcap(al_p) == 1);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    ok = ok && (stdlib_alloc_hasvalidcap(al_old) == 0);
    ok = ok && (stdlib_alloc_hasvalidcap((const void *)0) == 0);
    mt_report("allocator.hp", "stdlib_alloc_hasvalidcap", ok);
}

static void t60_cap_covers(void) {
    void *al_p = stdlib_alloc(16);
    int ok;
    ok = (stdlib_alloc_cap_covers(al_p, 0, 16) == 1);
    ok = ok && (stdlib_alloc_cap_covers(al_p, 4, 12) == 1);
    ok = ok && (stdlib_alloc_cap_covers(al_p, 16, 0) == 1);
    ok = ok && (stdlib_alloc_cap_covers(al_p, 16, 1) == 0);
    ok = ok && (stdlib_alloc_cap_covers(al_p, 17, 0) == 0);
    ok = ok && (stdlib_alloc_cap_covers((const void *)0, 0, 1) == 0);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    mt_report("allocator.hp", "stdlib_alloc_cap_covers", ok);
}

static void t61_cap_check(void) {
    void *al_p = stdlib_alloc(16);
    void *al_q;
    unsigned long long al_g;
    int ok;
    if (al_p == (void *)0) {
        mt_report("allocator.hp", "stdlib_alloc_cap_check", 0);
        return;
    }
    al_g = stdlib_alloc_generation(al_p);
    ok = (stdlib_alloc_cap_check(al_p, al_g, 0, 8) == 1);
    ok = ok && (stdlib_alloc_cap_check(al_p, al_g, 8, 8) == 1);
    ok = ok && (stdlib_alloc_cap_check(al_p, al_g, 8, 9) == 0);
    ok = ok && (stdlib_alloc_cap_check(al_p, 0, 0, 1) == 0);
    al_q = stdlib_realloc(al_p, 32);
    ok = ok && (stdlib_alloc_cap_check(al_q, al_g, 0, 1) == 0);
    if (al_q != (void *)0)
        stdlib_release(al_q);
    mt_report("allocator.hp", "stdlib_alloc_cap_check", ok);
}

static void t62_tm_snapshot(void) {
    void *al_p;
    stdlib_telemetry_stats al_snap;
    int ok;
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    al_p = stdlib_alloc(9);
    stdlib_alloc_tm_snapshot(&al_snap);
    ok = (al_snap.alloc_count == 1 && al_snap.bytes_allocated == 9);
    stdlib_alloc_tm_snapshot((stdlib_telemetry_stats *)0); /* no-op */
    if (al_p != (void *)0)
        stdlib_release(al_p);
    mt_report("allocator.hp", "stdlib_alloc_tm_snapshot", ok);
}

static void t63_tm_delta(void) {
    stdlib_telemetry_stats al_b;
    stdlib_telemetry_stats al_a;
    stdlib_telemetry_stats al_d;
    int ok;
    __builtin_memset(&al_b, 0, sizeof(al_b));
    __builtin_memset(&al_a, 0, sizeof(al_a));
    al_b.alloc_count = 10; al_b.free_count = 2; al_b.bytes_allocated = 100;
    al_b.alloc_failures = 1; al_b.invalid_ops = 4; al_b.bounds_violations = 5;
    al_b.ownership_violations = 6; al_b.lifecycle_failures = 7;
    al_b.container_growths = 8; al_b.api_failures = 9;
    al_a.alloc_count = 13; al_a.free_count = 5; al_a.bytes_allocated = 140;
    al_a.alloc_failures = 3; al_a.invalid_ops = 9; al_a.bounds_violations = 11;
    al_a.ownership_violations = 13; al_a.lifecycle_failures = 15;
    al_a.container_growths = 17; al_a.api_failures = 19;
    stdlib_alloc_tm_delta(&al_b, &al_a, &al_d);
    ok = (al_d.alloc_count == 3 && al_d.free_count == 3 &&
          al_d.bytes_allocated == 40 && al_d.alloc_failures == 2 &&
          al_d.invalid_ops == 5 && al_d.bounds_violations == 6 &&
          al_d.ownership_violations == 7 && al_d.lifecycle_failures == 8 &&
          al_d.container_growths == 9 && al_d.api_failures == 10);
    stdlib_alloc_tm_delta((const stdlib_telemetry_stats *)0, &al_a, &al_d);
    ok = ok && (al_d.alloc_count == 0 && al_d.api_failures == 0);
    stdlib_alloc_tm_delta(&al_b, &al_a, (stdlib_telemetry_stats *)0); /* no-op */
    mt_report("allocator.hp", "stdlib_alloc_tm_delta", ok);
}

static void t64_tm_counter(void) {
    int ok;
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_tm_add(STDLIB_TM_ALLOC, 7);
    ok = (stdlib_alloc_tm_counter(STDLIB_TM_ALLOC) == 7);
    ok = ok && (stdlib_alloc_tm_counter(-1) == 0);
    ok = ok && (stdlib_alloc_tm_counter(STDLIB_TM_COUNT) == 0);
    stdlib_telemetry_reset();
    mt_report("allocator.hp", "stdlib_alloc_tm_counter", ok);
}

static void t65_fail_begin(void) {
    void *al_p;
    int ok;
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_begin(ALLOC_FAIL_SCOPE_AT, 1);
    al_p = stdlib_alloc(8);
    ok = (al_p == (void *)0);
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_begin(ALLOC_FAIL_SCOPE_AFTER, 1);
    al_p = stdlib_alloc(8);
    ok = ok && (al_p != (void *)0);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    al_p = stdlib_alloc(8);
    ok = ok && (al_p == (void *)0);
    stdlib_alloc_fail_begin(0, 0); /* "anything else" resets */
    al_p = stdlib_alloc(8);
    ok = ok && (al_p != (void *)0);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    stdlib_alloc_fail_reset();
    mt_report("allocator.hp", "stdlib_alloc_fail_begin", ok);
}

static void t66_fail_end(void) {
    void *al_p;
    int ok;
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_begin(ALLOC_FAIL_SCOPE_AT, 1);
    stdlib_alloc_fail_end();
    al_p = stdlib_alloc(8);
    ok = (al_p != (void *)0);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    stdlib_alloc_fail_end(); /* idempotent */
    mt_report("allocator.hp", "stdlib_alloc_fail_end", ok);
}

static void t67_scope_ctor(void) {
    void *al_p;
    int ok;
    stdlib_alloc_fail_reset();
    {
        alloc_fail_scope al_s = alloc_fail_scope(ALLOC_FAIL_SCOPE_AT, 1);
        ok = (al_s.armed_now() == 1);
        al_p = stdlib_alloc(8);
        ok = ok && (al_p == (void *)0);
    }
    al_p = stdlib_alloc(8);
    ok = ok && (al_p != (void *)0); /* destructor reset injection */
    if (al_p != (void *)0)
        stdlib_release(al_p);
    stdlib_alloc_fail_reset();
    mt_report("allocator.hp", "alloc_fail_scope::alloc_fail_scope", ok);
}

static void t68_scope_armed_now(void) {
    int ok;
    alloc_fail_scope al_s = alloc_fail_scope(ALLOC_FAIL_SCOPE_NONE, 0);
    ok = (al_s.armed_now() == 1);
    al_s.reset_now();
    ok = ok && (al_s.armed_now() == 0);
    al_s.drop();
    mt_report("allocator.hp", "alloc_fail_scope::armed_now", ok);
}

static void t69_scope_reset_now(void) {
    void *al_p;
    int ok;
    stdlib_alloc_fail_reset();
    alloc_fail_scope al_s = alloc_fail_scope(ALLOC_FAIL_SCOPE_AFTER, 0);
    al_p = stdlib_alloc(8);
    ok = (al_p == (void *)0); /* injection active inside scope */
    al_s.reset_now();
    ok = ok && (al_s.armed_now() == 0);
    al_p = stdlib_alloc(8);
    ok = ok && (al_p != (void *)0);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    mt_report("allocator.hp", "alloc_fail_scope::reset_now", ok);
}

static void t70_scope_drop(void) {
    void *al_p;
    int ok;
    stdlib_alloc_fail_reset();
    alloc_fail_scope al_s = alloc_fail_scope(ALLOC_FAIL_SCOPE_AT, 1);
    al_s.drop();
    ok = (al_s.armed_now() == 0);
    al_p = stdlib_alloc(8);
    ok = ok && (al_p != (void *)0);
    if (al_p != (void *)0)
        stdlib_release(al_p);
    mt_report("allocator.hp", "alloc_fail_scope::drop", ok);
}

/* ======================================================================= */
/* stdlib/memory.hp                                                           */
/* ======================================================================= */

static void t71_mem_capacity(void) {
    void *me_p = stdlib_alloc(27);
    int ok;
    ok = (stdlib_mem_capacity(me_p) == 27);
    ok = ok && (stdlib_mem_capacity((const void *)0) == 0);
    if (me_p != (void *)0)
        stdlib_release(me_p);
    mt_report("memory.hp", "stdlib_mem_capacity", ok);
}

static void t72_mem_count(void) {
    void *me_c = stdlib_calloc(3, 9);
    size_t me_n = 99;
    int ok;
    ok = (stdlib_mem_count(me_c, 0, &me_n) == 0 && me_n == 3);
    me_n = 99;
    ok = ok && (stdlib_mem_count(me_c, 3, &me_n) == 0 && me_n == 9);
    me_n = 99;
    ok = ok && (stdlib_mem_count((const void *)0, 0, &me_n) == -1 && me_n == 0);
    if (me_c != (void *)0)
        stdlib_release(me_c);
    mt_report("memory.hp", "stdlib_mem_count", ok);
}

static void t73_mem_token(void) {
    void *me_p = stdlib_alloc(8);
    int ok;
    ok = (stdlib_mem_token(me_p) > 0 &&
          stdlib_mem_token(me_p) == stdlib_alloc_generation(me_p));
    ok = ok && (stdlib_mem_token((const void *)0) == 0);
    if (me_p != (void *)0)
        stdlib_release(me_p);
    mt_report("memory.hp", "stdlib_mem_token", ok);
}

static void t74_mem_check_token(void) {
    void *me_p = stdlib_alloc(8);
    error me_e = error();
    unsigned long long me_g;
    int ok;
    if (me_p == (void *)0) {
        mt_report("memory.hp", "stdlib_mem_check_token", 0);
        return;
    }
    me_g = stdlib_mem_token(me_p);
    error_reset(&me_e);
    ok = (stdlib_mem_check_token(me_p, me_g, &me_e) == STDLIB_OK);
    ok = ok && (stdlib_mem_check_token(me_p, 0, &me_e) == STDLIB_OK); /* opt-out */
    error_reset(&me_e);
    ok = ok && (stdlib_mem_check_token(me_p, me_g + 1, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_LIFECYCLE);
    ok = ok && (stdlib_mem_check_token(me_p, me_g, (error *)0) == STDLIB_OK);
    stdlib_release(me_p);
    mt_report("memory.hp", "stdlib_mem_check_token", ok);
}

static void t75_mem_check(void) {
    void *me_p = stdlib_alloc(8);
    void *me_saved = me_p;
    error me_e = error();
    unsigned long long me_g;
    int e_local = 1;
    int ok;
    if (me_p == (void *)0) {
        mt_report("memory.hp", "stdlib_mem_check", 0);
        return;
    }
    me_g = stdlib_mem_token(me_p);
    error_reset(&me_e);
    ok = (stdlib_mem_check(me_p, me_g, &me_e) == STDLIB_OK);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_check((const void *)0, 0, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_INVALID);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_check(&e_local, 0, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_INVALID);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_check(me_p, me_g + 1, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_LIFECYCLE);
    stdlib_release(me_p);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_check(me_saved, 0, &me_e) == STDLIB_FAIL);
    mt_report("memory.hp", "stdlib_mem_check", ok);
}

static void t76_mem_span_ok(void) {
    void *me_p = stdlib_alloc(16);
    int ok;
    ok = (stdlib_mem_span_ok(me_p, 16, 0, 16) == 1);
    ok = ok && (stdlib_mem_span_ok(me_p, 16, 4, 12) == 1);
    ok = ok && (stdlib_mem_span_ok(me_p, 16, 16, 0) == 1);
    ok = ok && (stdlib_mem_span_ok(me_p, 16, 0, 17) == 0);
    ok = ok && (stdlib_mem_span_ok(me_p, 16, 17, 0) == 0);
    ok = ok && (stdlib_mem_span_ok((const void *)0, 16, 0, 1) == 0);
    ok = ok && (stdlib_mem_span_ok(me_p, 0, 0, 0) == 0);
    if (me_p != (void *)0)
        stdlib_release(me_p);
    mt_report("memory.hp", "stdlib_mem_span_ok", ok);
}

static void t77_mem_span_contains(void) {
    void *me_p = stdlib_alloc(32);
    const void *me_base = me_p;
    int ok;
    if (me_p == (void *)0) {
        mt_report("memory.hp", "stdlib_mem_span_contains", 0);
        return;
    }
    ok = (stdlib_mem_span_contains(me_base, 32, (const char *)me_p + 4, 8) == 1);
    ok = ok && (stdlib_mem_span_contains(me_base, 32, (const char *)me_p + 24, 8) == 1);
    ok = ok && (stdlib_mem_span_contains(me_base, 32, (const char *)me_p + 24, 9) == 0);
    ok = ok && (stdlib_mem_span_contains(me_base, 32, (const char *)me_p - 1, 1) == 0);
    ok = ok && (stdlib_mem_span_contains(me_base, 32, (const void *)0, 1) == 0);
    ok = ok && (stdlib_mem_span_contains(me_base, 0, me_p, 0) == 0);
    stdlib_release(me_p);
    mt_report("memory.hp", "stdlib_mem_span_contains", ok);
}

static void t78_mem_bounds(void) {
    void *me_p = stdlib_alloc(14);
    int me_local = 4;
    const void *me_b = &me_local;
    size_t me_sz = 99;
    int ok;
    ok = (stdlib_mem_bounds(me_p, &me_b, &me_sz) == 0 && me_b == me_p && me_sz == 14);
    me_b = &me_local;
    me_sz = 99;
    ok = ok && (stdlib_mem_bounds((const void *)0, &me_b, &me_sz) == -1 &&
                me_b == (const void *)0 && me_sz == 0);
    ok = ok && (stdlib_mem_bounds(me_p, (const void **)0, &me_sz) == -1);
    if (me_p != (void *)0)
        stdlib_release(me_p);
    mt_report("memory.hp", "stdlib_mem_bounds", ok);
}

static void t79_mem_key_current(void) {
    void *me_p = stdlib_alloc(8);
    void *me_q;
    unsigned long long me_g;
    int ok;
    if (me_p == (void *)0) {
        mt_report("memory.hp", "stdlib_mem_key_current", 0);
        return;
    }
    me_g = stdlib_mem_token(me_p);
    ok = (stdlib_mem_key_current(me_p, 0) == 1);
    ok = ok && (stdlib_mem_key_current(me_p, me_g) == 1);
    ok = ok && (stdlib_mem_key_current(me_p, me_g + 1) == 0);
    me_q = stdlib_realloc(me_p, 16);
    ok = ok && (stdlib_mem_key_current(me_q, me_g) == 0);
    if (me_q != (void *)0)
        stdlib_release(me_q);
    mt_report("memory.hp", "stdlib_mem_key_current", ok);
}

static void t80_mem_query(void) {
    void *me_p = stdlib_alloc(16);
    unsigned long long me_g;
    error me_e = error();
    int ok;
    if (me_p == (void *)0) {
        mt_report("memory.hp", "stdlib_mem_query", 0);
        return;
    }
    me_g = stdlib_mem_token(me_p);
    error_reset(&me_e);
    ok = (stdlib_mem_query(me_p, 0, 16, me_g, &me_e) == STDLIB_OK);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_query(me_p, 8, 16, me_g, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_BOUNDS);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_query(me_p, 0, 1, me_g + 1, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_LIFECYCLE);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_query((const void *)0, 0, 1, 0, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_INVALID);
    stdlib_release(me_p);
    mt_report("memory.hp", "stdlib_mem_query", ok);
}

static void t81_mem_check_range(void) {
    void *me_p = stdlib_alloc(16);
    int e_local = 2;
    error me_e = error();
    int ok;
    if (me_p == (void *)0) {
        mt_report("memory.hp", "stdlib_mem_check_range", 0);
        return;
    }
    error_reset(&me_e);
    ok = (stdlib_mem_check_range(me_p, 0, 16, 1, &me_e) == STDLIB_OK);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_check_range(me_p, 8, 9, 1, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_BOUNDS);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_check_range((const void *)0, 0, 1, 1, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_INVALID);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_check_range(&e_local, 0, 1, 0, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_INVALID);
    stdlib_release(me_p);
    mt_report("memory.hp", "stdlib_mem_check_range", ok);
}

static void t82_mem_copy(void) {
    void *me_src = stdlib_alloc(16);
    void *me_dst = stdlib_alloc(16);
    unsigned char me_in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    unsigned char me_out[4];
    unsigned long long me_g;
    error me_e = error();
    int ok;
    if (me_src == (void *)0 || me_dst == (void *)0) {
        mt_report("memory.hp", "stdlib_mem_copy", 0);
        return;
    }
    __builtin_memcpy(me_src, me_in, 8);
    me_g = stdlib_mem_token(me_dst);
    error_reset(&me_e);
    ok = (stdlib_mem_copy(me_dst, 4, me_src, 2, 4, me_g, &me_e) == STDLIB_OK &&
          me_e.ok() == 1);
    __builtin_memcpy(me_out, (const char *)me_dst + 4, 4);
    ok = ok && (me_out[0] == 3 && me_out[1] == 4 && me_out[2] == 5 && me_out[3] == 6);
    ok = ok && (stdlib_mem_copy(me_dst, 0, me_src, 0, 0, me_g, &me_e) == STDLIB_OK);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_copy(me_dst, 14, me_src, 0, 4, me_g, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_BOUNDS);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_copy(me_dst, 0, me_src, 0, 1, me_g + 1, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_LIFECYCLE);
    stdlib_release(me_src);
    stdlib_release(me_dst);
    mt_report("memory.hp", "stdlib_mem_copy", ok);
}

static void t83_mem_move(void) {
    void *me_p = stdlib_alloc(16);
    unsigned char me_in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    unsigned char me_out[8];
    unsigned long long me_g;
    error me_e = error();
    int ok;
    if (me_p == (void *)0) {
        mt_report("memory.hp", "stdlib_mem_move", 0);
        return;
    }
    __builtin_memcpy(me_p, me_in, 8);
    me_g = stdlib_mem_token(me_p);
    error_reset(&me_e);
    ok = (stdlib_mem_move(me_p, 2, me_p, 0, 6, me_g, &me_e) == STDLIB_OK);
    __builtin_memcpy(me_out, me_p, 8);
    ok = ok && (me_out[0] == 1 && me_out[1] == 2 && me_out[2] == 1 &&
                me_out[3] == 2 && me_out[4] == 3 && me_out[5] == 4 &&
                me_out[6] == 5 && me_out[7] == 6);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_move(me_p, 14, me_p, 0, 4, me_g, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_BOUNDS);
    stdlib_release(me_p);
    mt_report("memory.hp", "stdlib_mem_move", ok);
}

static void t84_mem_set(void) {
    void *me_p = stdlib_alloc(16);
    unsigned char me_in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    unsigned char me_out[8];
    unsigned long long me_g;
    error me_e = error();
    int ok;
    if (me_p == (void *)0) {
        mt_report("memory.hp", "stdlib_mem_set", 0);
        return;
    }
    __builtin_memcpy(me_p, me_in, 8);
    me_g = stdlib_mem_token(me_p);
    error_reset(&me_e);
    ok = (stdlib_mem_set(me_p, 2, 0x5a, 5, me_g, &me_e) == STDLIB_OK);
    __builtin_memcpy(me_out, me_p, 8);
    ok = ok && (me_out[0] == 1 && me_out[1] == 2 && me_out[2] == 0x5a &&
                me_out[3] == 0x5a && me_out[4] == 0x5a && me_out[5] == 0x5a &&
                me_out[6] == 0x5a && me_out[7] == 8);
    ok = ok && (stdlib_mem_set(me_p, 0, 0x11, 0, me_g, &me_e) == STDLIB_OK);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_set(me_p, 2, 0x33, 5, me_g + 1, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_LIFECYCLE);
    __builtin_memcpy(me_out, me_p, 8);
    ok = ok && (me_out[2] == 0x5a); /* failed set wrote nothing */
    stdlib_release(me_p);
    mt_report("memory.hp", "stdlib_mem_set", ok);
}

static void t85_mem_borrow(void) {
    void *me_p = stdlib_alloc(16);
    unsigned long long me_g;
    error me_e = error();
    int ok;
    if (me_p == (void *)0) {
        mt_report("memory.hp", "stdlib_mem_borrow", 0);
        return;
    }
    me_g = stdlib_mem_token(me_p);
    error_reset(&me_e);
    ok = (stdlib_mem_borrow(me_p, 4, 8, me_g, &me_e) == STDLIB_OK);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_borrow(me_p, 8, 9, me_g, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_BOUNDS);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_borrow(me_p, 0, 1, me_g + 1, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_LIFECYCLE);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_borrow((const void *)0, 0, 1, 0, &me_e) == STDLIB_FAIL);
    stdlib_release(me_p);
    mt_report("memory.hp", "stdlib_mem_borrow", ok);
}

static void t86_mem_transfer(void) {
    void *me_src = stdlib_alloc(12);
    void *me_dst = (void *)0;
    void *me_other;
    void *me_empty = (void *)0;
    unsigned long long me_g0;
    error me_e = error();
    int ok;
    if (me_src == (void *)0) {
        mt_report("memory.hp", "stdlib_mem_transfer", 0);
        return;
    }
    me_g0 = stdlib_mem_token(me_src);
    error_reset(&me_e);
    ok = (stdlib_mem_transfer(&me_dst, &me_src, &me_e) == STDLIB_OK);
    ok = ok && (me_dst != (void *)0 && me_src == (void *)0 &&
                stdlib_alloc_generation(me_dst) > me_g0 && me_e.ok() == 1);
    error_reset(&me_e);
    ok = ok && (stdlib_mem_transfer((void **)0, &me_dst, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_OWNERSHIP);
    error_reset(&me_e);
    me_other = me_dst;
    ok = ok && (stdlib_mem_transfer(&me_other, &me_dst, &me_e) == STDLIB_FAIL &&
                me_e.category() == STDLIB_CATEGORY_OWNERSHIP);
    error_reset(&me_e);
    me_other = (void *)0;
    ok = ok && (stdlib_mem_transfer(&me_other, &me_empty, &me_e) == STDLIB_FAIL);
    if (me_dst != (void *)0)
        stdlib_release(me_dst);
    mt_report("memory.hp", "stdlib_mem_transfer", ok);
}

static void t87_mem_release(void) {
    void *me_p = stdlib_alloc(12);
    const void *me_old = me_p;
    int ok;
    if (me_p == (void *)0) {
        mt_report("memory.hp", "stdlib_mem_release", 0);
        return;
    }
    stdlib_mem_release(&me_p);
    ok = (me_p == (void *)0 && stdlib_alloc_size(me_old) == 0);
    stdlib_mem_release(&me_p); /* second call is a no-op */
    stdlib_mem_release((void **)0); /* NULL is a no-op */
    ok = ok && (me_p == (void *)0);
    mt_report("memory.hp", "stdlib_mem_release", ok);
}

int main(void) {
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();

    t01_count_size_overflow();
    t02_size_class_round_up();
    t03_size_class_words();
    t04_size_normalize_zero();

    t05_tm_add();
    t06_telemetry_set_enabled();
    t07_telemetry_is_enabled();
    t08_telemetry_reset();
    t09_telemetry_snapshot();
    t10_set_error_callback();
    t11_category_name();
    t12_note();
    t13_alloc_fail_at();
    t14_alloc_fail_after();
    t15_alloc_fail_reset();
    t16_alloc_seq();
    t17_meta_acquire();
    t18_meta_release_lock();
    t19_meta_hash();
    t20_meta_find();
    t21_meta_record();
    t22_meta_forget();
    t23_alloc();
    t24_calloc();
    t25_realloc();
    t26_safe_div();
    t27_alloc_size();
    t28_alloc_elem_size();
    t29_alloc_length();
    t30_alloc_generation();
    t31_alloc_check_generation();
    t32_alloc_query_length();
    t33_transfer_ownership();
    t34_release();
    t35_error_ctor();
    t36_error_ok();
    t37_error_failed_now();
    t38_error_code();
    t39_error_what();
    t40_error_category();
    t41_error_clear();
    t42_error_set();
    t43_error_set_category();
    t44_error_reset();
    t45_error_fail();
    t46_error_fail_cat();

    t47_alloc_identity();
    t48_alloc_id_equal();
    t49_alloc_id_matches();
    t50_view_bytes();
    t51_view_elem_bytes();
    t52_view_generation();
    t53_view_live();
    t54_view_count();
    t55_view_remaining();
    t56_view_unchanged();
    t57_view_transfer_ok();
    t58_cap_state();
    t59_hasvalidcap();
    t60_cap_covers();
    t61_cap_check();
    t62_tm_snapshot();
    t63_tm_delta();
    t64_tm_counter();
    t65_fail_begin();
    t66_fail_end();
    t67_scope_ctor();
    t68_scope_armed_now();
    t69_scope_reset_now();
    t70_scope_drop();

    t71_mem_capacity();
    t72_mem_count();
    t73_mem_token();
    t74_mem_check_token();
    t75_mem_check();
    t76_mem_span_ok();
    t77_mem_span_contains();
    t78_mem_bounds();
    t79_mem_key_current();
    t80_mem_query();
    t81_mem_check_range();
    t82_mem_copy();
    t83_mem_move();
    t84_mem_set();
    t85_mem_borrow();
    t86_mem_transfer();
    t87_mem_release();

    stdlib_alloc_fail_reset();
    stdlib_set_error_callback((stdlib_error_callback)0);
    print("METHOD_TEST", "alloc_mem", mt_total, mt_passed, mt_failed, 0);
    return mt_failed ? 1 : 0;
}
