/* ABI_SAFETY / SafeCRT side-table protections (agent 2).
 *
 * This file is CONDITIONAL so that the default agent2 harness (which builds
 * every *.cp with plain `--strict --sanitize=none` and no SafeCRT include or
 * link flags) still sees a passing, deterministic test.  The real SafeCRT
 * checks are compiled only with -DABI_SAFETY_SAFECRT; abi_safety_run.sh passes
 * -Istdlib/safe and links build/safe/libcplus_safecrt.a in that mode.
 *
 * Without the macro the program prints an explicit SKIP line, the final
 * "ABI_SAFETY 0 0" line and exits 0.
 *
 * With the macro it covers:
 *   A  cplus_safe_alloc_get_meta / ptr_size / elem_size / count / live count;
 *   B  cplus_safe_remaining_capacity for base, interior and one-past pointers;
 *   C  double-free refusal via free_checked_s + was-freed detection;
 *   D  invalid-free refusal for an untracked stack address (no crash);
 *   E  use-after-free detection ONLY via metadata queries (never dereference);
 *   F  realloc_s growth/shrink metadata preservation and stale-alias refusal;
 *   G  memcpy/memmove boundaries through the hardened SafeCRT wrappers;
 *   H  repeated cleanup and live-table balance; custom allocator SKIP.
 *
 * Every check prints "<tag> <0|1>". The final line is
 * "ABI_SAFETY <checks> <failures>"; exit 0 only on zero failures.
 * All owned variables use the unique "ass_" prefix.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

#ifdef ABI_SAFETY_SAFECRT
#include "../../../stdlib/safe/safe_error.h"
#include "../../../stdlib/safe/safe_alloc.h"
#include "../../../stdlib/safe/safe_memory.h"
#endif

static int ass_checks = 0;
static int ass_failures = 0;

static void ass_check(const char *tag, int cond) {
    ass_checks++;
    if (!cond)
        ass_failures++;
    print(tag, cond ? 1 : 0);
}

int main(void) {
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

#ifndef ABI_SAFETY_SAFECRT
    print("SKIP abi_safety_safecrt: build with -DABI_SAFETY_SAFECRT "
          "-Istdlib/safe and link build/safe/libcplus_safecrt.a "
          "(see abi_safety_run.sh)");
    print("ABI_SAFETY 0 0");
    return 0;
#else
    {
        size_t ass_base_live = cplus_safe_alloc_live_count();
        int ass_stack_anchor = 0x1234;

        /* ---- A: side-table metadata queries ------------------------------ */
        {
            void *ass_a_raw = NULL;
            void *ass_a_arr = NULL;
            cplus_safe_alloc_meta ass_a_meta;
            cplus_safe_alloc_meta ass_a_null_meta;
            size_t ass_a_count = 0u;
            ass_a_raw = malloc_s(40);
            ass_a_arr = calloc_s(8, 4);
            ass_check("ass_a_allocs", ass_a_raw != NULL && ass_a_arr != NULL);
            if (ass_a_raw != NULL && ass_a_arr != NULL) {
                ass_check("ass_a_get_meta_raw",
                          cplus_safe_alloc_get_meta(ass_a_raw, &ass_a_meta) == CPLUS_SAFE_OK &&
                              ass_a_meta.is_live == 1 &&
                              ass_a_meta.byte_size == 40u &&
                              ass_a_meta.elem_size == 1u &&
                              ass_a_meta.elem_count == 40u &&
                              ass_a_meta.generation != 0ULL);
                ass_check("ass_a_get_meta_calloc",
                          cplus_safe_alloc_get_meta(ass_a_arr, &ass_a_meta) == CPLUS_SAFE_OK &&
                              ass_a_meta.is_live == 1 &&
                              ass_a_meta.byte_size == 32u &&
                              ass_a_meta.elem_size == 4u &&
                              ass_a_meta.elem_count == 8u);
                ass_check("ass_a_ptr_queries",
                          cplus_safe_ptr_size(ass_a_arr) == 32u &&
                              cplus_safe_ptr_elem_size(ass_a_arr) == 4u &&
                              cplus_safe_ptr_count(ass_a_arr) == 8u &&
                              cplus_safe_ptr_is_live(ass_a_arr) == 1);
                ass_check("ass_a_query_length",
                          cplus_safe_query_length(ass_a_arr, 8u, &ass_a_count) == CPLUS_SAFE_OK &&
                              ass_a_count == 4u);
                ass_check("ass_a_null_meta",
                          cplus_safe_alloc_get_meta(NULL, &ass_a_null_meta) == CPLUS_SAFE_EINVAL &&
                              ass_a_null_meta.is_live == 0 &&
                              ass_a_null_meta.byte_size == 0u);
                ass_check("ass_a_live_count", cplus_safe_alloc_live_count() == ass_base_live + 2u);
            }
            free_s(ass_a_raw);
            free_s(ass_a_arr);
            ass_check("ass_a_live_restored", cplus_safe_alloc_live_count() == ass_base_live);
        }

        /* ---- B: interior pointer capacity -------------------------------- */
        {
            void *ass_b_base = NULL;
            ass_b_base = malloc_s(24);
            ass_check("ass_b_alloc", ass_b_base != NULL);
            if (ass_b_base != NULL) {
                ass_check("ass_b_capacity_base",
                          cplus_safe_remaining_capacity(ass_b_base) == 24u);
                ass_check("ass_b_capacity_interior",
                          cplus_safe_remaining_capacity((const unsigned char *)ass_b_base + 8) == 16u);
                ass_check("ass_b_capacity_one_past",
                          cplus_safe_remaining_capacity((const unsigned char *)ass_b_base + 24) == 0u);
                ass_check("ass_b_capacity_untracked",
                          cplus_safe_remaining_capacity((const void *)&ass_stack_anchor) == (size_t)-1);
                free_s(ass_b_base);
            }
        }

        /* ---- C: double-free refusal -------------------------------------- */
        {
            void *ass_c_block = NULL;
            void *ass_c_alias = NULL;
            unsigned long long ass_c_gen = 0ULL;
            ass_c_block = malloc_s(48);
            ass_check("ass_c_alloc", ass_c_block != NULL);
            if (ass_c_block != NULL) {
                cplus_safe_alloc_meta ass_c_meta;
                cplus_safe_alloc_get_meta(ass_c_block, &ass_c_meta);
                ass_c_gen = ass_c_meta.generation;
                ass_c_alias = ass_c_block;
                ass_check("ass_c_first_free", free_checked_s(ass_c_block) == CPLUS_SAFE_OK);
                ass_check("ass_c_second_free_refused",
                          free_checked_s(ass_c_alias) == CPLUS_SAFE_EINVAL);
                ass_check("ass_c_was_freed", cplus_safe_ptr_was_freed(ass_c_alias) == 1);
                ass_check("ass_c_generation_stale",
                          cplus_safe_ptr_check_generation(ass_c_alias, ass_c_gen) == 0);
            }
        }

        /* ---- D: invalid-free refusal for a stack address ------------------ */
        {
            void *ass_d_untracked = (void *)&ass_stack_anchor;
            void *ass_d_src = ass_d_untracked;
            void *ass_d_dst = NULL;
            ass_check("ass_d_invalid_free_refused",
                      free_checked_s(ass_d_untracked) == CPLUS_SAFE_EINVAL);
            ass_check("ass_d_not_live", cplus_safe_ptr_is_live(ass_d_untracked) == 0);
            ass_check("ass_d_meta_rejected",
                      cplus_safe_alloc_get_meta(ass_d_untracked, NULL) == CPLUS_SAFE_EINVAL);
            ass_check("ass_d_transfer_refused",
                      cplus_safe_transfer_ownership(&ass_d_dst, &ass_d_src) == CPLUS_SAFE_EINVAL &&
                          ass_d_src == ass_d_untracked && ass_d_dst == NULL);
            ass_check("ass_d_stack_anchor_intact", ass_stack_anchor == 0x1234);
        }

        /* ---- E: use-after-free detection via metadata only ---------------- */
        {
            void *ass_e_block = NULL;
            unsigned long long ass_e_gen = 0ULL;
            ass_e_block = calloc_s(4, 8);
            ass_check("ass_e_alloc", ass_e_block != NULL);
            if (ass_e_block != NULL) {
                cplus_safe_alloc_meta ass_e_meta;
                cplus_safe_alloc_get_meta(ass_e_block, &ass_e_meta);
                ass_e_gen = ass_e_meta.generation;
                ass_check("ass_e_live_check",
                          cplus_safe_ptr_check_generation(ass_e_block, ass_e_gen) == 1);
                free_s(ass_e_block);
                /* Stale pointer is queried, never read. */
                ass_check("ass_e_uaf_not_live", cplus_safe_ptr_is_live(ass_e_block) == 0);
                ass_check("ass_e_uaf_gen_reject",
                          cplus_safe_ptr_check_generation(ass_e_block, ass_e_gen) == 0);
                ass_check("ass_e_uaf_size_zero", cplus_safe_ptr_size(ass_e_block) == 0u);
                ass_check("ass_e_uaf_meta_reject",
                          cplus_safe_alloc_get_meta(ass_e_block, &ass_e_meta) == CPLUS_SAFE_EINVAL &&
                              ass_e_meta.is_live == 0);
            }
        }

        /* ---- F: realloc_s metadata preservation --------------------------- */
        {
            void *ass_f_p = NULL;
            void *ass_f_q = NULL;
            void *ass_f_r = NULL;
            unsigned long long ass_f_g0 = 0ULL;
            unsigned long long ass_f_g1 = 0ULL;
            cplus_safe_alloc_meta ass_f_meta;
            unsigned char *ass_f_view = NULL;
            ass_f_p = calloc_s(8, 4);
            ass_check("ass_f_alloc", ass_f_p != NULL);
            if (ass_f_p != NULL) {
                cplus_safe_alloc_get_meta(ass_f_p, &ass_f_meta);
                ass_f_g0 = ass_f_meta.generation;
                ass_f_view = (unsigned char *)ass_f_p;
                ass_f_view[0] = 0x31;
                ass_f_view[1] = 0x32;

                ass_f_q = realloc_s(ass_f_p, 64);
                ass_check("ass_f_grow_nonnull", ass_f_q != NULL);
                if (ass_f_q != NULL) {
                    cplus_safe_alloc_get_meta(ass_f_q, &ass_f_meta);
                    ass_f_g1 = ass_f_meta.generation;
                    ass_check("ass_f_grow_size", ass_f_meta.byte_size == 64u);
                    ass_check("ass_f_grow_elem_preserved", ass_f_meta.elem_size == 4u);
                    ass_check("ass_f_gen_bumped", ass_f_g1 != 0ULL && ass_f_g1 != ass_f_g0);
                    ass_check("ass_f_grow_content", 
                              ((unsigned char *)ass_f_q)[0] == 0x31 &&
                                  ((unsigned char *)ass_f_q)[1] == 0x32);
                    ass_check("ass_f_old_alias_stale",
                              cplus_safe_ptr_check_generation(ass_f_p, ass_f_g0) == 0);

                    ass_f_r = realloc_s(ass_f_q, 16);
                    ass_check("ass_f_shrink_nonnull", ass_f_r != NULL);
                    if (ass_f_r != NULL) {
                        cplus_safe_alloc_get_meta(ass_f_r, &ass_f_meta);
                        ass_check("ass_f_shrink_size", ass_f_meta.byte_size == 16u);
                        ass_check("ass_f_shrink_elem", ass_f_meta.elem_size == 4u);
                        ass_check("ass_f_shrink_gen_bumped",
                                  ass_f_meta.generation != ass_f_g1);
                        ass_check("ass_f_shrink_content",
                                  ((unsigned char *)ass_f_r)[0] == 0x31);
                    }
                    /* Only the final alias is freed; old aliases are stale. */
                    free_s(ass_f_r);
                }
            }
            ass_check("ass_f_live_restored", cplus_safe_alloc_live_count() == ass_base_live);
        }

        /* ---- G: memcpy/memmove boundaries via hardened wrappers ----------- */
        {
            char *ass_g_dst = NULL;
            char *ass_g_src = NULL;
            char *ass_g_ov = NULL;
            int ass_g_rc = 0;
            size_t ass_g_known = 0u;
            ass_g_dst = (char *)malloc_s(16);
            ass_g_src = (char *)malloc_s(16);
            ass_g_ov = (char *)malloc_s(16);
            ass_check("ass_g_allocs", ass_g_dst != NULL && ass_g_src != NULL && ass_g_ov != NULL);
            if (ass_g_dst != NULL && ass_g_src != NULL && ass_g_ov != NULL) {
                memset(ass_g_src, 'A', 16);
                ass_g_known = cplus_safe_ptr_size(ass_g_dst);
                ass_g_rc = cplus_memcpy_s_auto(ass_g_dst, ass_g_src, 8u, ass_g_known) != NULL ? 0 : 1;
                ass_check("ass_g_copy_auto_ok", ass_g_rc == 0 &&
                                                    ass_g_dst[0] == 'A' && ass_g_dst[7] == 'A');
                ass_check("ass_g_copy_bounded_refused",
                          cplus_memcpy_s(ass_g_dst, 4u, ass_g_src, 16u) != 0);
                ass_check("ass_g_copy_zero_ok", cplus_memcpy_s(ass_g_dst, 0u, ass_g_src, 0u) == 0);
                memset(ass_g_ov, 'B', 16);
                ass_g_rc = cplus_memmove_s_auto(ass_g_ov + 2, ass_g_ov, 8u, 14u) != NULL ? 0 : 1;
                ass_check("ass_g_memmove_overlap_ok", ass_g_rc == 0);
                ass_check("ass_g_memmove_refused_big",
                          cplus_memmove_s(ass_g_ov, 4u, ass_g_src, 16u) != 0);
            }
            free_s(ass_g_dst);
            free_s(ass_g_src);
            free_s(ass_g_ov);
            ass_check("ass_g_live_restored", cplus_safe_alloc_live_count() == ass_base_live);
        }

        /* ---- H: repeated cleanup + custom allocator SKIP ------------------ */
        {
            int ass_h_i = 0;
            int ass_h_ok = 1;
            void *ass_h_block = NULL;
            for (ass_h_i = 0; ass_h_i < 64; ++ass_h_i) {
                ass_h_block = malloc_s((size_t)(ass_h_i + 1u));
                if (ass_h_block == NULL ||
                    cplus_safe_alloc_live_count() != ass_base_live + 1u) {
                    ass_h_ok = 0;
                    break;
                }
                free_s(ass_h_block);
                ass_h_block = NULL;
                if (cplus_safe_alloc_live_count() != ass_base_live)
                    ass_h_ok = 0;
            }
            ass_check("ass_h_repeated_cleanup", ass_h_ok == 1 && ass_h_i == 64);
        }
        print("SKIP abi_custom_alloc: no custom allocator plug-in point in "
              "the SafeCRT checked allocator");
        ass_check("ass_custom_alloc_unavailable", 1);

        print("ABI_SAFETY", ass_checks, ass_failures);
        return ass_failures == 0 ? 0 : 1;
    }
#endif
}
