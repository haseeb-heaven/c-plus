/* Minimal self-test for stdlib/allocator.hp (additive allocation diagnostics,
 * failure scopes and telemetry snapshots).  Self-checking: each check prints
 * "<tag> <0|1>" and the final line is "ALLOCATOR_TEST <checks> <failures>".
 * Exit 0 only when failures == 0.  No check dereferences a stale pointer.
 *
 * Strict-build patterns: every local has a function-unique prefix (C+ name
 * resolution is translation-unit wide); no pointer-parameter subscripts and
 * no variable division/modulo are written here; no chained temporaries. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/allocator.hp"

static int al_checks = 0;
static int al_failures = 0;

static void al_check(const char *tag, int cond) {
    al_checks++;
    if (!cond)
        al_failures++;
    print(tag, cond ? 1 : 0);
}

/* ---- identity, scalar views, reallocation history, stale views ---------- */
static void al_identity_tests(void) {
    stdlib_alloc_id ait_id;
    stdlib_alloc_id ait_id2;
    unsigned long long ait_gen;
    size_t ait_n;
    int ait_untracked = 7;
    void *ait_base;
    void *ait_resized;
    void *ait_stale;

    ait_base = stdlib_calloc(4, 8); /* 32 bytes, elem size 8 */
    if (ait_base == (void *)0) {
        al_check("ALLOCATOR_SETUP", 0);
        return;
    }
    ait_n = stdlib_alloc_identity(ait_base, &ait_id);
    al_check("id_live", ait_n == 0 && ait_id.base == ait_base &&
             ait_id.generation != 0 && ait_id.byte_size == 32 &&
             ait_id.elem_size == 8 &&
             stdlib_alloc_id_matches(&ait_id, ait_base) == 1);

    /* NULL and untracked identities: out-parameter zeroed, no match. */
    ait_n = stdlib_alloc_identity((const void *)0, &ait_id2);
    al_check("id_null", ait_n == -1 && ait_id2.base == (const void *)0 &&
             ait_id2.generation == 0 && ait_id2.byte_size == 0 &&
             stdlib_alloc_id_equal(&ait_id2, &ait_id) == 0 &&
             stdlib_alloc_id_equal((const stdlib_alloc_id *)0, &ait_id) == 0 &&
             stdlib_alloc_id_matches(&ait_id2, ait_base) == 0 &&
             stdlib_alloc_id_matches(&ait_id, (const void *)0) == 0);
    ait_n = stdlib_alloc_identity((const void *)&ait_untracked, &ait_id2);
    al_check("id_untracked", ait_n == -1 &&
             ait_id2.base == (const void *)0 &&
             stdlib_alloc_view_bytes((const void *)&ait_untracked) == 0 &&
             stdlib_alloc_view_live((const void *)&ait_untracked) == 0 &&
             stdlib_alloc_view_generation((const void *)&ait_untracked) == 0);

    al_check("view_fields", stdlib_alloc_view_bytes(ait_base) == 32 &&
             stdlib_alloc_view_elem_bytes(ait_base) == 8 &&
             stdlib_alloc_view_generation(ait_base) == ait_id.generation &&
             stdlib_alloc_view_live(ait_base) == 1 &&
             stdlib_alloc_view_count(ait_base) == 4);
    al_check("view_remaining", stdlib_alloc_view_remaining(ait_base, 0) == 32 &&
             stdlib_alloc_view_remaining(ait_base, 20) == 12 &&
             stdlib_alloc_view_remaining(ait_base, 32) == 0 &&
             stdlib_alloc_view_remaining(ait_base, 33) == 0 &&
             stdlib_alloc_view_remaining((const void *)0, 0) == 0);

    /* A resize bumps the generation and retires the old identity. */
    ait_gen = stdlib_alloc_view_generation(ait_base);
    al_check("unchanged_stable", stdlib_alloc_view_unchanged(ait_base, ait_gen) == 1 &&
             stdlib_alloc_view_unchanged(ait_base, 0) == 0 &&
             stdlib_alloc_view_unchanged((const void *)0, ait_gen) == 0);
    ait_resized = stdlib_realloc(ait_base, 64);
    al_check("realloc_stale", ait_resized != (void *)0 &&
             stdlib_alloc_view_unchanged(ait_base, ait_gen) == 0 &&
             stdlib_alloc_id_matches(&ait_id, ait_base) == 0);
    if (ait_resized != (void *)0) {
        ait_n = stdlib_alloc_identity(ait_resized, &ait_id2);
        al_check("realloc_new_id", ait_n == 0 && ait_id2.generation != ait_gen &&
                 stdlib_alloc_id_equal(&ait_id, &ait_id2) == 0 &&
                 stdlib_alloc_view_bytes(ait_resized) == 64 &&
                 stdlib_alloc_view_live(ait_resized) == 1);
        stdlib_release(ait_resized);
    }

    /* A released block is stale: views are safe and refuse identity. */
    ait_stale = stdlib_alloc(16);
    if (ait_stale != (void *)0) {
        ait_n = stdlib_alloc_identity(ait_stale, &ait_id);
        stdlib_release(ait_stale);
        al_check("view_stale", ait_n == 0 &&
                 stdlib_alloc_view_bytes(ait_stale) == 0 &&
                 stdlib_alloc_view_live(ait_stale) == 0 &&
                 stdlib_alloc_identity(ait_stale, &ait_id2) == -1 &&
                 stdlib_alloc_id_matches(&ait_id, ait_stale) == 0);
    }
}

/* ---- ownership transfer validation -------------------------------------- */
static void al_transfer_tests(void) {
    stdlib_alloc_id att_id;
    size_t att_n;
    void *att_src;
    void *att_dst;

    att_src = stdlib_alloc(24);
    if (att_src == (void *)0)
        return;
    att_n = stdlib_alloc_identity(att_src, &att_id);
    att_dst = (void *)0;
    al_check("transfer_valid", att_n == 0 &&
             stdlib_alloc_view_transfer_ok(&att_dst, &att_src) == 1 &&
             stdlib_alloc_view_transfer_ok(&att_dst, (void **)0) == 0 &&
             stdlib_alloc_view_transfer_ok((void **)0, &att_src) == 0 &&
             stdlib_alloc_view_transfer_ok(&att_src, &att_src) == 0);
    if (stdlib_transfer_ownership(&att_dst, &att_src) == 0) {
        al_check("transfer_stale", att_src == (void *)0 &&
                 att_dst != (void *)0 &&
                 stdlib_alloc_id_matches(&att_id, att_dst) == 0);
        stdlib_release(att_dst);
    }
}

/* ---- telemetry snapshots and deltas ------------------------------------- */
static void al_telemetry_tests(void) {
    stdlib_telemetry_stats atm_before;
    stdlib_telemetry_stats atm_after;
    stdlib_telemetry_stats atm_delta;
    void *atm_a;
    void *atm_b;
    void *atm_c;

    stdlib_telemetry_reset();
    stdlib_alloc_tm_snapshot(&atm_before);
    atm_a = stdlib_alloc(8);
    atm_b = stdlib_alloc(8);
    atm_c = stdlib_alloc(8);
    stdlib_alloc_tm_snapshot(&atm_after);
    stdlib_alloc_tm_delta(&atm_before, &atm_after, &atm_delta);
    print("DBG before_alloc", atm_before.alloc_count);
    print("DBG after_alloc", atm_after.alloc_count);
    print("DBG delta_alloc", atm_delta.alloc_count);
    print("DBG delta_bytes", atm_delta.bytes_allocated);
    print("DBG delta_free", atm_delta.free_count);
    print("DBG counter_alloc", stdlib_alloc_tm_counter(STDLIB_TM_ALLOC));
    al_check("tm_delta", atm_delta.alloc_count == 3 &&
             atm_delta.bytes_allocated == 24 && atm_delta.free_count == 0 &&
             stdlib_alloc_tm_counter(STDLIB_TM_ALLOC) == 3 &&
             stdlib_alloc_tm_counter(-1) == 0 &&
             stdlib_alloc_tm_counter(STDLIB_TM_COUNT) == 0);
    stdlib_alloc_tm_snapshot((stdlib_telemetry_stats *)0);
    stdlib_alloc_tm_delta((const stdlib_telemetry_stats *)0, &atm_after,
                          &atm_delta);
    al_check("tm_null", atm_delta.alloc_count == 0 &&
             atm_delta.api_failures == 0);
    stdlib_release(atm_a);
    stdlib_release(atm_b);
    stdlib_release(atm_c);
}

/* ---- RAII and explicit allocation-failure scopes ------------------------ */
static void al_scope_tests(void) {
    void *asc_at;
    void *asc_after;
    void *asc_explicit;

    /* RAII scope: injection is reset automatically at scope exit. */
    {
        alloc_fail_scope asc_scope = alloc_fail_scope(ALLOC_FAIL_SCOPE_AT, 1);
        al_check("scope_at_armed", asc_scope.armed_now() == 1);
        asc_at = stdlib_alloc(8);
        al_check("scope_at_fail", asc_at == (void *)0);
    }
    asc_at = stdlib_alloc(8);
    al_check("scope_at_reset", asc_at != (void *)0);
    if (asc_at != (void *)0)
        stdlib_release(asc_at);

    {
        alloc_fail_scope asc_scope2 = alloc_fail_scope(ALLOC_FAIL_SCOPE_AFTER, 0);
        asc_after = stdlib_alloc(8);
        al_check("scope_after_fail", asc_after == (void *)0);
        asc_scope2.reset_now();
        al_check("scope_reset_now", asc_scope2.armed_now() == 0);
        asc_after = stdlib_alloc(8);
        al_check("scope_after_recover", asc_after != (void *)0);
        if (asc_after != (void *)0)
            stdlib_release(asc_after);
    }

    /* Explicit begin/end pair (documented non-nestable caveats). */
    stdlib_alloc_fail_begin(ALLOC_FAIL_SCOPE_AT, 1);
    asc_explicit = stdlib_alloc(8);
    al_check("explicit_fail", asc_explicit == (void *)0);
    stdlib_alloc_fail_end();
    asc_explicit = stdlib_alloc(8);
    al_check("explicit_end", asc_explicit != (void *)0);
    if (asc_explicit != (void *)0)
        stdlib_release(asc_explicit);
}

int main(void) {
    al_identity_tests();
    al_transfer_tests();
    al_telemetry_tests();
    al_scope_tests();
    print("ALLOCATOR_TEST", al_checks, al_failures);
    return al_failures == 0 ? 0 : 1;
}
