/* Deterministic allocation-failure injection suite -- hook semantics.
 *
 * This fixture pins the three documented injection hooks in stdlib/error.hp
 * (stdlib_alloc_fail_at / _fail_after / _fail_reset) and the three failure
 * shapes required by the task:
 *   * fail the FIRST allocation  (fail_at(1))
 *   * fail the Nth allocation    (fail_at(3), one-shot: 1 and 2 succeed, 4 succeeds)
 *   * fail a REALLOCATION        (vector::reserve / push_back growth)
 * plus fail_after(0/1), reset, telemetry accounting and repeated cleanup.
 *
 * METHOD. Injection is a logical counter inside the checked allocator. Every
 * injection is paired with stdlib_alloc_fail_reset() in the same scope, every
 * allocated block is released, and telemetry is asserted balanced
 * (alloc_count == free_count) after cleanup. No system memory is exhausted and
 * no allocation is attempted in a loop until malloc fails.
 *
 * STRICT-SAFE PATTERNS. Out-parameters are initialised at declaration, raw
 * pointers are cast to char* and compared with a typed NULL, container names
 * are unique per function (C+ lowers a method call by the owning variable's
 * name), and every tag is printed as "<tag> <1|0>" via af_ck().
 *
 * Output: one "<tag> <1|0>" per check, then "ALLOC_FAIL <checks> <failures>".
 * Exit code 0 only when failures == 0.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

static int hk_checks = 0;
static int hk_failures = 0;

static void hk_ck(const char *tag, int cond) {
    hk_checks++;
    if (!cond)
        hk_failures++;
    print(tag, cond ? 1 : 0);
}

/* ---- fail the first allocation ------------------------------------------- */
static void hk_first(void) {
    char *hk_p1 = (char *)0;
    char *hk_p2 = (char *)0;
    long hk_seq = 0L;
    stdlib_telemetry_stats hk_tm;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    /* fail_at(1): the very next allocation fails exactly once. */
    stdlib_alloc_fail_at(1);
    hk_p1 = (char *)stdlib_alloc(16);
    hk_seq = stdlib_alloc_seq(); /* capture before reset: reset clears seq */
    stdlib_alloc_fail_reset();

    hk_ck("hk_first_null", hk_p1 == (char *)0);
    hk_ck("hk_first_seq", hk_seq == 1L);
    stdlib_telemetry_snapshot(&hk_tm);
    hk_ck("hk_first_failcnt", hk_tm.alloc_failures == 1ULL);
    hk_ck("hk_first_balanced", hk_tm.alloc_count == hk_tm.free_count);

    /* Recovery after reset: next allocation succeeds, release balances. */
    hk_p2 = (char *)stdlib_alloc(16);
    hk_ck("hk_first_retry", hk_p2 != (char *)0);
    stdlib_release(hk_p2);
    stdlib_telemetry_snapshot(&hk_tm);
    hk_ck("hk_first_retry_balanced",
          hk_tm.alloc_count == hk_tm.free_count && hk_tm.alloc_count == 1ULL);
    stdlib_release(hk_p1); /* NULL is a documented no-op: repeated cleanup safe */
    stdlib_telemetry_set_enabled(0);
}

/* ---- fail the Nth allocation (one-shot) ---------------------------------- */
static void hk_nth(void) {
    char *hk_a = (char *)0;
    char *hk_b = (char *)0;
    char *hk_c = (char *)0;
    char *hk_d = (char *)0;
    long hk_seq = 0L;
    stdlib_telemetry_stats hk_tm;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    stdlib_alloc_fail_at(3);
    hk_a = (char *)stdlib_alloc(8); /* seq 1: succeeds */
    hk_b = (char *)stdlib_alloc(8); /* seq 2: succeeds */
    hk_c = (char *)stdlib_alloc(8); /* seq 3: FAILS once */
    hk_d = (char *)stdlib_alloc(8); /* seq 4: succeeds (one-shot) */
    hk_seq = stdlib_alloc_seq();    /* capture before reset */
    stdlib_alloc_fail_reset();

    hk_ck("hk_nth_a", hk_a != (char *)0);
    hk_ck("hk_nth_b", hk_b != (char *)0);
    hk_ck("hk_nth_c_null", hk_c == (char *)0);
    hk_ck("hk_nth_d_ok", hk_d != (char *)0);
    hk_ck("hk_nth_seq", hk_seq == 4L);
    stdlib_telemetry_snapshot(&hk_tm);
    hk_ck("hk_nth_failcnt", hk_tm.alloc_failures == 1ULL);

    /* Repeated cleanup after the failure point. */
    stdlib_release(hk_a);
    stdlib_release(hk_b);
    stdlib_release(hk_c); /* NULL no-op */
    stdlib_release(hk_d);
    stdlib_telemetry_snapshot(&hk_tm);
    hk_ck("hk_nth_balanced",
          hk_tm.alloc_count == hk_tm.free_count && hk_tm.alloc_count == 3ULL);
    stdlib_telemetry_set_enabled(0);
}

/* ---- fail_after(n): n succeed, every later one fails until reset ---------- */
static void hk_after(void) {
    char *hk_e = (char *)0;
    char *hk_f = (char *)0;
    char *hk_g = (char *)0;
    char *hk_h = (char *)0;
    stdlib_telemetry_stats hk_tm;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    /* fail_after(1): seq 1 succeeds, seq >= 2 fails until reset. */
    stdlib_alloc_fail_after(1);
    hk_e = (char *)stdlib_alloc(8); /* seq 1: succeeds */
    hk_f = (char *)stdlib_alloc(8); /* seq 2: fails */
    hk_g = (char *)stdlib_alloc(8); /* seq 3: fails */
    hk_ck("hk_after_first_ok", hk_e != (char *)0);
    hk_ck("hk_after_second_null", hk_f == (char *)0);
    hk_ck("hk_after_third_null", hk_g == (char *)0);
    stdlib_telemetry_snapshot(&hk_tm);
    hk_ck("hk_after_failcnt", hk_tm.alloc_failures == 2ULL);

    /* Cleanup while the injection is STILL active: release must not allocate. */
    stdlib_release(hk_e);
    hk_ck("hk_after_cleanup_active", hk_tm.alloc_failures == 2ULL);
    stdlib_alloc_fail_reset();

    /* Recovery: after reset the next allocation succeeds. */
    hk_h = (char *)stdlib_alloc(8);
    hk_ck("hk_after_retry", hk_h != (char *)0);
    stdlib_release(hk_h);
    stdlib_telemetry_snapshot(&hk_tm);
    hk_ck("hk_after_balanced", hk_tm.alloc_count == hk_tm.free_count);
    stdlib_telemetry_set_enabled(0);
}

/* ---- reallocation: vector reserve and push_back growth ------------------- */
static void hk_realloc(void) {
    vector hk_vec = vector();
    int hk_o = 0;
    int i;
    stdlib_telemetry_stats hk_tm;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    for (i = 0; i < 4; i++)
        hk_vec.push_back(i * 10); /* cap 0 -> 4 via pushes */

    /* Reallocation point 1: reserve(64) from capacity 4 is a single alloc. */
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_at(1);
    hk_ck("hk_realloc_reserve_fail", hk_vec.reserve(64) == -1);
    stdlib_alloc_fail_reset();
    hk_ck("hk_realloc_reserve_state",
          hk_vec.size() == 4 && hk_vec.capacity() == 4);
    hk_o = 0;
    for (i = 0; i < 4; i++) {
        if (hk_vec.at_checked((size_t)i, &hk_o) != 0 || hk_o != i * 10)
            break;
    }
    hk_ck("hk_realloc_reserve_contents", i == 4);
    hk_ck("hk_realloc_reserve_retry",
          hk_vec.reserve(64) == 0 && hk_vec.capacity() == 64);

    /* Fill the reserved capacity so the next push_back MUST reallocate. */
    for (i = 4; i < 64; i++)
        hk_vec.push_back(i * 10);

    /* Reallocation point 2: a full-capacity push_back must grow (64 -> 128). */
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_at(1);
    hk_ck("hk_realloc_push_fail", hk_vec.push_back(999) == -1);
    stdlib_alloc_fail_reset();
    hk_ck("hk_realloc_push_state",
          hk_vec.size() == 64 && hk_vec.capacity() == 64);
    hk_o = 0;
    hk_ck("hk_realloc_push_contents",
          hk_vec.at_checked(63, &hk_o) == 0 && hk_o == 630);
    hk_ck("hk_realloc_push_retry",
          hk_vec.push_back(999) == 0 && hk_vec.size() == 65 &&
              hk_vec.capacity() == 128);
    hk_o = 0;
    hk_ck("hk_realloc_push_value",
          hk_vec.at_checked(64, &hk_o) == 0 && hk_o == 999);

    /* Repeated cleanup after each failure point. */
    hk_vec.drop();
    hk_vec.drop(); /* idempotent */
    stdlib_telemetry_snapshot(&hk_tm);
    hk_ck("hk_realloc_cleaned",
          hk_tm.alloc_count == hk_tm.free_count && hk_tm.alloc_failures >= 2 &&
              hk_tm.alloc_count > 0);
    stdlib_telemetry_set_enabled(0);
}

int main(void) {
    hk_first();
    hk_nth();
    hk_after();
    hk_realloc();
    stdlib_alloc_fail_reset();
    print("ALLOC_FAIL", hk_checks, hk_failures);
    return hk_failures ? 1 : 0;
}
