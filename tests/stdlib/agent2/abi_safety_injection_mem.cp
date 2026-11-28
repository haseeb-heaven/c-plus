/* ABI_SAFETY / allocation failure injection and bounded copy (agent 2).
 *
 * Self-checking, deterministic, no stdin, no external library.
 *
 * Covered here:
 *   A  stdlib_alloc_fail_at(n): exactly the n-th subsequent allocation fails
 *      once, with stdlib_alloc_seq() and telemetry alloc_failures tracking;
 *   B  stdlib_alloc_fail_after(n): the first n allocations succeed, every
 *      later one fails until stdlib_alloc_fail_reset();
 *   C  reset / disabled injection / oversized and overflowing requests;
 *   D  metadata-gated memcpy/memmove boundaries: a test-local hardened copy
 *      wrapper refuses untracked, undersized and overlapping buffers using
 *      only stdlib side-table metadata (the SafeCRT hardened wrappers are
 *      exercised in abi_safety_safecrt.cp);
 *   E  repeated cleanup and side-table balance.
 *
 * Every check prints "<tag> <0|1>". The final line is
 * "ABI_SAFETY <checks> <failures>"; exit 0 only on zero failures.
 * All owned variables use the unique "asim_" prefix.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static int asim_checks = 0;
static int asim_failures = 0;

static void asim_check(const char *tag, int cond) {
    asim_checks++;
    if (!cond)
        asim_failures++;
    print(tag, cond ? 1 : 0);
}

/* Refuse a copy unless both buffers are live stdlib blocks with enough
 * recorded capacity and their byte ranges do not overlap. Never touches a
 * rejected buffer. Returns 0 on success, -1 on refusal. */
static int asim_checked_copy(void *dst, const void *src_base,
                            size_t src_offset, size_t n) {
    size_t dsize;
    size_t ssize;
    uintptr_t d;
    uintptr_t s;
    if (dst == NULL || src_base == NULL || n == 0u)
        return -1;
    dsize = stdlib_alloc_size(dst);
    ssize = stdlib_alloc_size(src_base); /* interior pointers are not tracked */
    if (dsize < n || src_offset > ssize || n > ssize - src_offset)
        return -1;
    d = (uintptr_t)dst;
    s = (uintptr_t)src_base + src_offset;
    if (d < s + n && s < d + n)
        return -1;
    memcpy(dst, (const unsigned char *)src_base + src_offset, n);
    return 0;
}

/* Same as above but performs a memmove for overlapping ranges. */
static int asim_checked_move(void *dst, const void *src_base,
                            size_t src_offset, size_t n) {
    size_t dsize;
    size_t ssize;
    if (dst == NULL || src_base == NULL || n == 0u)
        return -1;
    dsize = stdlib_alloc_size(dst);
    ssize = stdlib_alloc_size(src_base);
    if (dsize < n || src_offset > ssize || n > ssize - src_offset)
        return -1;
    memmove(dst, (const unsigned char *)src_base + src_offset, n);
    return 0;
}

int main(void) {
    stdlib_telemetry_stats asim_pre;
    stdlib_telemetry_stats asim_post;
    size_t asim_base_live = 0;
    int asim_untracked = 77;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    asim_base_live = stdlib_state.meta_live;

    /* ---- A: fail_at(n) --------------------------------------------------- */
    {
        void *asim_a1 = NULL;
        void *asim_a2 = NULL;
        void *asim_a3 = NULL;
        void *asim_a4 = NULL;
        void *asim_a_failone = NULL;
        void *asim_a_recover = NULL;
        stdlib_telemetry_snapshot(&asim_pre);
        stdlib_alloc_fail_at(3);
        asim_check("asim_a_seq_reset", stdlib_alloc_seq() == 0L);
        asim_a1 = stdlib_alloc(8);
        asim_check("asim_a_first_ok", asim_a1 != NULL && stdlib_alloc_seq() == 1L);
        asim_a2 = stdlib_alloc(8);
        asim_check("asim_a_second_ok", asim_a2 != NULL && stdlib_alloc_seq() == 2L);
        asim_a3 = stdlib_alloc(8);
        asim_check("asim_a_third_fails", asim_a3 == NULL && stdlib_alloc_seq() == 3L);
        asim_a4 = stdlib_alloc(8);
        asim_check("asim_a_fourth_recovers", asim_a4 != NULL && stdlib_alloc_seq() == 4L);
        stdlib_telemetry_snapshot(&asim_post);
        asim_check("asim_a_fail_counter",
                   asim_post.alloc_failures - asim_pre.alloc_failures == 1ULL);
        asim_check("asim_a_success_count",
                   asim_post.alloc_count - asim_pre.alloc_count == 3ULL);
        stdlib_release(asim_a1);
        stdlib_release(asim_a2);
        stdlib_release(asim_a4);

        /* n == 1 fails the very next allocation. */
        stdlib_alloc_fail_at(1);
        asim_a_failone = stdlib_alloc(8);
        asim_check("asim_a_fail_at_one", asim_a_failone == NULL);
        stdlib_alloc_fail_reset();
        asim_a_recover = stdlib_alloc(8);
        asim_check("asim_a_after_reset", asim_a_recover != NULL);
        stdlib_release(asim_a_recover);
    }
    asim_check("asim_a_live_restored", stdlib_state.meta_live == asim_base_live);

    /* ---- B: fail_after(n) ------------------------------------------------ */
    {
        void *asim_b1 = NULL;
        void *asim_b2 = NULL;
        void *asim_b3 = NULL;
        void *asim_b4 = NULL;
        void *asim_b5 = NULL;
        void *asim_b6 = NULL;
        stdlib_telemetry_snapshot(&asim_pre);
        stdlib_alloc_fail_after(2);
        asim_b1 = stdlib_alloc(8);
        asim_b2 = stdlib_alloc(8);
        asim_b3 = stdlib_alloc(8);
        asim_b4 = stdlib_alloc(8);
        asim_b5 = stdlib_alloc(8);
        asim_check("asim_b_first_two_ok", asim_b1 != NULL && asim_b2 != NULL);
        asim_check("asim_b_later_fail",
                   asim_b3 == NULL && asim_b4 == NULL && asim_b5 == NULL);
        asim_check("asim_b_seq_five", stdlib_alloc_seq() == 5L);
        stdlib_telemetry_snapshot(&asim_post);
        asim_check("asim_b_fail_counter",
                   asim_post.alloc_failures - asim_pre.alloc_failures == 3ULL);
        stdlib_alloc_fail_reset();
        asim_b6 = stdlib_alloc(8);
        asim_check("asim_b_recovers_after_reset", asim_b6 != NULL);
        stdlib_release(asim_b1);
        stdlib_release(asim_b2);
        stdlib_release(asim_b6);
    }
    asim_check("asim_b_live_restored", stdlib_state.meta_live == asim_base_live);

    /* ---- C: disabled injection and refusal without an attempt ------------ */
    {
        void *asim_c1 = NULL;
        void *asim_c2 = NULL;
        stdlib_telemetry_snapshot(&asim_pre);
        stdlib_alloc_fail_at(0);
        asim_c1 = stdlib_alloc(16);
        asim_check("asim_c_at_zero_off", asim_c1 != NULL);
        stdlib_alloc_fail_after(-1);
        asim_c2 = stdlib_alloc(16);
        asim_check("asim_c_after_negative_off", asim_c2 != NULL);
        stdlib_alloc_fail_reset();

        asim_check("asim_c_oversize_refused",
                   stdlib_alloc(STDLIB_ALLOC_LIMIT + 1u) == NULL);
        asim_check("asim_c_calloc_overflow_refused",
                   stdlib_calloc((size_t)2, STDLIB_ALLOC_LIMIT) == NULL);
        stdlib_telemetry_snapshot(&asim_post);
        asim_check("asim_c_no_injection_failures",
                   asim_post.alloc_failures - asim_pre.alloc_failures == 2ULL);
        stdlib_release(asim_c1);
        stdlib_release(asim_c2);
    }
    asim_check("asim_c_live_restored", stdlib_state.meta_live == asim_base_live);

    /* ---- D: metadata-gated copy boundaries -------------------------------- */
    {
        void *asim_d_dst = NULL;
        void *asim_d_src = NULL;
        unsigned char *asim_d_view = NULL;
        asim_d_dst = stdlib_alloc(8);
        asim_d_src = stdlib_alloc(8);
        asim_check("asim_d_allocs", asim_d_dst != NULL && asim_d_src != NULL);
        if (asim_d_dst != NULL && asim_d_src != NULL) {
            asim_d_view = (unsigned char *)asim_d_src;
            asim_d_view[0] = 0x5A;
            asim_d_view[7] = 0xA5;
            asim_check("asim_d_copy_ok",
                       asim_checked_copy(asim_d_dst, asim_d_src, 0u, 8u) == 0 &&
                           ((unsigned char *)asim_d_dst)[0] == 0x5A &&
                           ((unsigned char *)asim_d_dst)[7] == 0xA5);
            /* Oversized copy is refused before any write. */
            asim_d_view = (unsigned char *)asim_d_dst;
            asim_d_view[0] = 0x01;
            asim_check("asim_d_oversize_refused",
                       asim_checked_copy(asim_d_dst, asim_d_src, 0u, 9u) == -1 &&
                           asim_d_view[0] == 0x01);
            asim_check("asim_d_untracked_refused",
                       asim_checked_copy(asim_d_dst, (void *)&asim_untracked,
                                         0u, 4u) == -1);
            asim_check("asim_d_zero_refused",
                       asim_checked_copy(asim_d_dst, asim_d_src, 0u, 0u) == -1);
            /* Overlap refused by copy, accepted by move. */
            asim_check("asim_d_overlap_copy_refused",
                       asim_checked_copy(asim_d_dst, asim_d_dst, 1u, 4u) == -1);
            asim_check("asim_d_overlap_move_ok",
                       asim_checked_move(asim_d_dst, asim_d_dst, 1u, 4u) == 0);
        }
        stdlib_release(asim_d_dst);
        stdlib_release(asim_d_src);
    }
    asim_check("asim_d_live_restored", stdlib_state.meta_live == asim_base_live);

    /* ---- E: repeated cleanup --------------------------------------------- */
    {
        int asim_e_i = 0;
        int asim_e_ok = 1;
        void *asim_e_block = NULL;
        for (asim_e_i = 0; asim_e_i < 64; ++asim_e_i) {
            asim_e_block = stdlib_alloc(16);
            if (asim_e_block == NULL ||
                stdlib_alloc_size(asim_e_block) != 16u) {
                asim_e_ok = 0;
                break;
            }
            stdlib_release(asim_e_block);
            asim_e_block = NULL;
            if (stdlib_state.meta_live != asim_base_live)
                asim_e_ok = 0;
        }
        asim_check("asim_e_repeated_cleanup", asim_e_ok == 1 && asim_e_i == 64);
    }
    asim_check("asim_e_final_live", stdlib_state.meta_live == asim_base_live);

    print("ABI_SAFETY", asim_checks, asim_failures);
    return asim_failures == 0 ? 0 : 1;
}
