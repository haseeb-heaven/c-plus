/* ABI_SAFETY / allocation metadata integrity and side-table queries (agent 2).
 *
 * Complements abi_safety_alloc_lifecycle.cp with size/length queries, the
 * runtime side-table "get_meta" equivalent (cplus_alloc_bytes), derived
 * interior-pointer capacity, and cplus_realloc growth/shrink behaviour.
 * Self-checking, deterministic, no stdin, no external library.
 *
 * Covered here:
 *   A  stdlib_alloc_size / elem_size / length / query_length on stdlib_alloc
 *      and stdlib_calloc blocks, including NULL and untracked probes;
 *   B  runtime side-table metadata (cplus_alloc_bytes) cross-checked with the
 *      stdlib side table; interior-pointer remaining capacity derived from
 *      the live base allocation (the dedicated SafeCRT capacity API is
 *      covered by abi_safety_safecrt.cp and noted as SKIP here);
 *   C  cplus_realloc growth / shrink / NULL-base / zero-size metadata
 *      correctness. error.hp has NO stdlib_realloc wrapper, so cplus_realloc
 *      is used as instructed; the observed stdlib-side-table divergence is a
 *      reproduced defect documented in ABI_SAFETY_NOTES.md and asserted here
 *      as a CHARACTERISATION (it must not be mistaken for correct behaviour).
 *
 * Every check prints "<tag> <0|1>". The final line is
 * "ABI_SAFETY <checks> <failures>"; exit 0 only on zero failures.
 * All owned variables use the unique "asmi_" prefix.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static int asmi_checks = 0;
static int asmi_failures = 0;

static void asmi_check(const char *tag, int cond) {
    asmi_checks++;
    if (!cond)
        asmi_failures++;
    print(tag, cond ? 1 : 0);
}

/* Derived remaining capacity: a live base pointer's runtime byte count minus
 * the interior offset.  Never dereferences the pointer; a non-live base or an
 * offset at/after the end yields 0. */
static size_t asmi_remaining(const void *base, const void *interior) {
    size_t bytes;
    uintptr_t b;
    uintptr_t p;
    if (base == NULL || interior == NULL)
        return 0u;
    if (stdlib_alloc_size(base) == 0u)
        return 0u;
    b = (uintptr_t)base;
    p = (uintptr_t)interior;
    if (p < b)
        return 0u;
    if (p - b >= (uintptr_t)SIZE_MAX)
        return 0u;
    bytes = cplus_alloc_bytes(base);
    if ((size_t)(p - b) >= bytes)
        return 0u;
    return bytes - (size_t)(p - b);
}

int main(void) {
    stdlib_telemetry_stats asmi_pre;
    stdlib_telemetry_stats asmi_post;
    size_t asmi_base_live = 0;
    int asmi_untracked = 11;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    asmi_base_live = stdlib_state.meta_live;

    /* ---- A: size / elem_size / length / query_length --------------------- */
    {
        void *asmi_a_raw = NULL;
        void *asmi_a_arr = NULL;
        size_t asmi_a_out = 999u;
        stdlib_telemetry_snapshot(&asmi_pre);
        asmi_a_raw = stdlib_alloc(40);
        asmi_a_arr = stdlib_calloc(8, 4);
        asmi_check("asmi_a_allocs", asmi_a_raw != NULL && asmi_a_arr != NULL);
        if (asmi_a_raw != NULL && asmi_a_arr != NULL) {
            asmi_check("asmi_a_raw_size", stdlib_alloc_size(asmi_a_raw) == 40u);
            asmi_check("asmi_a_raw_elem", stdlib_alloc_elem_size(asmi_a_raw) == 1u);
            asmi_check("asmi_a_raw_len", stdlib_alloc_length(asmi_a_raw) == 40u);
            asmi_check("asmi_a_arr_size", stdlib_alloc_size(asmi_a_arr) == 32u);
            asmi_check("asmi_a_arr_elem", stdlib_alloc_elem_size(asmi_a_arr) == 4u);
            asmi_check("asmi_a_arr_len", stdlib_alloc_length(asmi_a_arr) == 8u);
            asmi_a_out = 999u;
            asmi_check("asmi_a_query_default",
                       stdlib_alloc_query_length(asmi_a_raw, 0, &asmi_a_out) == 0 &&
                           asmi_a_out == 40u);
            asmi_a_out = 999u;
            asmi_check("asmi_a_query_elem8",
                       stdlib_alloc_query_length(asmi_a_raw, 8, &asmi_a_out) == 0 &&
                           asmi_a_out == 5u);
            asmi_check("asmi_a_null_size", stdlib_alloc_size(NULL) == 0u);
            asmi_check("asmi_a_null_query",
                       stdlib_alloc_query_length(NULL, 1u, &asmi_a_out) == -1);
            asmi_check("asmi_a_untracked_size",
                       stdlib_alloc_size((void *)&asmi_untracked) == 0u);
            {
                int asmi_a_i = 0;
                int asmi_a_zeroed = 1;
                unsigned char *asmi_a_view = (unsigned char *)asmi_a_arr;
                for (asmi_a_i = 0; asmi_a_i < 32; ++asmi_a_i)
                    if (asmi_a_view[asmi_a_i] != 0)
                        asmi_a_zeroed = 0;
                asmi_check("asmi_a_calloc_zeroed", asmi_a_zeroed == 1);
            }
        }
        if (asmi_a_raw != NULL)
            stdlib_release(asmi_a_raw);
        if (asmi_a_arr != NULL)
            stdlib_release(asmi_a_arr);
        stdlib_telemetry_snapshot(&asmi_post);
        asmi_check("asmi_a_balance",
                   asmi_post.alloc_count - asmi_pre.alloc_count == 2u &&
                       asmi_post.free_count - asmi_pre.free_count == 2u);
        asmi_check("asmi_a_live_restored", stdlib_state.meta_live == asmi_base_live);
    }

    /* ---- B: runtime side table get_meta + interior capacity -------------- */
    {
        void *asmi_b_base = NULL;
        unsigned char *asmi_b_view = NULL;
        stdlib_telemetry_snapshot(&asmi_pre);
        asmi_b_base = stdlib_alloc(24);
        asmi_check("asmi_b_alloc", asmi_b_base != NULL);
        if (asmi_b_base != NULL) {
            asmi_check("asmi_b_runtime_get_meta",
                       cplus_alloc_bytes(asmi_b_base) == 24u);
            asmi_check("asmi_b_tables_agree",
                       cplus_alloc_bytes(asmi_b_base) ==
                           stdlib_alloc_size(asmi_b_base));
            asmi_check("asmi_b_capacity_base",
                       asmi_remaining(asmi_b_base, asmi_b_base) == 24u);
            asmi_check("asmi_b_capacity_interior",
                       asmi_remaining(asmi_b_base,
                                      (const unsigned char *)asmi_b_base + 8) == 16u);
            asmi_check("asmi_b_capacity_one_past",
                       asmi_remaining(asmi_b_base,
                                      (const unsigned char *)asmi_b_base + 24) == 0u);
            asmi_check("asmi_b_capacity_untracked",
                       asmi_remaining(asmi_b_base,
                                      (const void *)&asmi_untracked) == 0u);
            asmi_b_view = (unsigned char *)asmi_b_base;
            asmi_b_view[0] = 0x11;
            asmi_b_view[23] = 0x22;
            asmi_check("asmi_b_inbounds_writes",
                       asmi_b_view[0] == 0x11 && asmi_b_view[23] == 0x22);
        }
        stdlib_release(asmi_b_base);
        stdlib_telemetry_snapshot(&asmi_post);
        asmi_check("asmi_b_balance",
                   asmi_post.alloc_count - asmi_pre.alloc_count == 1u &&
                       asmi_post.free_count - asmi_pre.free_count == 1u);
    }
    /* The dedicated interior-capacity API (cplus_safe_remaining_capacity)
       lives in the SafeCRT layer and is not reachable from the default c+
       stdlib harness; it is covered by abi_safety_safecrt.cp. */
    print("SKIP asmi_safe_remaining_capacity_api: SafeCRT-layer "
          "cplus_safe_remaining_capacity is covered by abi_safety_safecrt.cp");

    /* ---- C: cplus_realloc metadata (characterisation) -------------------- */
    {
        void *asmi_c_p = NULL;
        void *asmi_c_q = NULL;
        void *asmi_c_r = NULL;
        void *asmi_c_z = NULL;
        unsigned char *asmi_c_view = NULL;
        unsigned long long asmi_c_gen = 0ULL;
        stdlib_telemetry_snapshot(&asmi_pre);
        asmi_c_p = stdlib_alloc(32);
        asmi_check("asmi_c_alloc", asmi_c_p != NULL);
        if (asmi_c_p != NULL) {
            asmi_c_gen = stdlib_alloc_generation(asmi_c_p);
            asmi_c_view = (unsigned char *)asmi_c_p;
            asmi_c_view[0] = 0x41;
            asmi_c_view[1] = 0x42;
            asmi_c_view[2] = 0x43;

            /* Growth: runtime side table tracks the fresh block correctly. */
            asmi_c_q = cplus_realloc(asmi_c_p, 64);
            asmi_check("asmi_c_grow_nonnull", asmi_c_q != NULL);
            if (asmi_c_q != NULL) {
                asmi_check("asmi_c_grow_new_pointer", asmi_c_q != asmi_c_p);
                asmi_check("asmi_c_grow_runtime_size",
                           cplus_alloc_bytes(asmi_c_q) == 64u);
                asmi_c_view = (unsigned char *)asmi_c_q;
                asmi_check("asmi_c_grow_content_preserved",
                           asmi_c_view[0] == 0x41 && asmi_c_view[1] == 0x42 &&
                               asmi_c_view[2] == 0x43);

                /* CHARACTERISATION of a reproduced defect: error.hp has no
                   stdlib_realloc, so cplus_realloc leaves the stdlib side
                   table stale (old pointer still sized 32, new pointer 0). */
                asmi_check("asmi_c_grow_stdlib_defect_old_stale",
                           stdlib_alloc_size(asmi_c_p) == 32u &&
                               stdlib_alloc_generation(asmi_c_p) == asmi_c_gen);
                asmi_check("asmi_c_grow_stdlib_defect_new_untracked",
                           stdlib_alloc_size(asmi_c_q) == 0u &&
                               stdlib_alloc_generation(asmi_c_q) == 0ULL);
                /* The old alias is dead in the runtime: never release it.
                   Drop only its stdlib side-table entry. */
                stdlib_meta_forget(asmi_c_p);

                /* Shrink: runtime metadata follows the new size. */
                asmi_c_r = cplus_realloc(asmi_c_q, 16);
                asmi_check("asmi_c_shrink_nonnull", asmi_c_r != NULL);
                if (asmi_c_r != NULL) {
                    asmi_check("asmi_c_shrink_runtime_size",
                               cplus_alloc_bytes(asmi_c_r) == 16u);
                    asmi_c_view = (unsigned char *)asmi_c_r;
                    asmi_check("asmi_c_shrink_content_prefix",
                               asmi_c_view[0] == 0x41 && asmi_c_view[2] == 0x43);
                    stdlib_release(asmi_c_r);
                } else {
                    stdlib_release(asmi_c_q);
                }
            } else {
                stdlib_meta_forget(asmi_c_p);
            }

            /* NULL base behaves like a fresh allocation. */
            asmi_c_z = cplus_realloc(NULL, 24);
            asmi_check("asmi_c_null_base_alloc", asmi_c_z != NULL);
            if (asmi_c_z != NULL) {
                asmi_check("asmi_c_null_base_size",
                           cplus_alloc_bytes(asmi_c_z) == 24u);
                stdlib_release(asmi_c_z);
            }

            /* Zero size releases and returns NULL. */
            asmi_c_p = stdlib_alloc(12);
            asmi_check("asmi_c_zero_alloc", asmi_c_p != NULL);
            if (asmi_c_p != NULL) {
                asmi_c_gen = stdlib_alloc_generation(asmi_c_p);
                asmi_c_q = cplus_realloc(asmi_c_p, 0);
                asmi_check("asmi_c_zero_returns_null", asmi_c_q == NULL);
                stdlib_meta_forget(asmi_c_p);
                asmi_check("asmi_c_zero_meta_forgotten",
                           stdlib_alloc_generation(asmi_c_p) == 0ULL &&
                               asmi_c_gen != 0ULL);
            }
        }
        stdlib_telemetry_snapshot(&asmi_post);
        asmi_check("asmi_c_runtime_telemetry_unchanged",
                   asmi_post.alloc_count - asmi_pre.alloc_count == 2u);
    }

    print("ABI_SAFETY", asmi_checks, asmi_failures);
    return asmi_failures == 0 ? 0 : 1;
}
