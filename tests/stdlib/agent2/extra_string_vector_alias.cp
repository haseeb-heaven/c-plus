/* extra_string_vector_alias.cp — remaining UNTESTED string/vector alias paths.
 *
 * Scope assigned to agent 2 (final assignment list):
 *   A  string self-assignment via copy_from(this) documented no-op
 *   B  repeated append growth to a long string (incl. self/suffix append)
 *   C  embedded NUL bytes via explicit length-aware operations
 *      (string_view/stdlib_utf8_validate are length-aware; the `string`
 *      storage path is NOT — documented SKIP, with a check that confirms
 *      the limitation rather than pretending support)
 *   D  string_view at end / beyond end / after owner clear / after owner
 *      reallocation — POINTER + SIZE COMPARISON ONLY; a stale view is NEVER
 *      dereferenced (no equals(), no is_valid_utf8() on a stale pointer)
 *   E  repeated is_valid_utf8 on valid and invalid data
 *   F  vector self-assignment via copy_from(this) (supported, documented no-op)
 *   G  element preservation through 1000 fixed-seed push/pop operations
 *   H  stale slice refusal after clear() and erase()
 *   I  view() on an empty vector
 *
 * Fully self-checking: every check prints "<tag> <0|1>".  The final line is
 * "EXTRA_STRING_VECTOR_ALIAS <checks> <failures>" and the exit code is 0 only
 * when failures == 0.  All randomness comes from a fixed-seed LCG, so stdout
 * is byte-for-byte deterministic across runs and modes.  All owned locals use
 * unique `sva_`-prefixed names (C+ RAII lowering tracks owners by identifier
 * within a function).  No header, existing test, or other agent's file is
 * modified.  No stale pointer is ever dereferenced.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"

static int sva_checks = 0;
static int sva_failures = 0;

static void sva_check(const char *tag, int condition) {
    sva_checks++;
    if (!condition)
        sva_failures++;
    print(tag, condition ? 1 : 0);
}

/* Fixed-seed LCG (Numerical Recipes constants); unsigned wrap is defined. */
static unsigned int sva_lcg_state = 0x5A17C0DEu;

static void sva_lcg_reseed(unsigned int seed) {
    sva_lcg_state = seed;
}

static unsigned int sva_lcg_next(void) {
    sva_lcg_state = sva_lcg_state * 1664525u + 1013904223u;
    return sva_lcg_state;
}

int main(void) {
    int sva_final_rc = 0;

    /* ------------------------------------------------------------------ A */
    {
        string sva_self_str = string("self-assignment-payload");
        const char *sva_self_ptr = sva_self_str.c_str();
        size_t sva_self_size = sva_self_str.size();
        size_t sva_self_cap = sva_self_str.capacity();
        int sva_self_i = 0;
        int sva_self_repeat_ok = 1;
        string sva_empty_self_str = string("");

        sva_check("sva_str_self_copy_rc",
                  sva_self_str.copy_from(&sva_self_str) == 0);
        for (sva_self_i = 0; sva_self_i < 8; sva_self_i++)
            if (sva_self_str.copy_from(&sva_self_str) != 0)
                sva_self_repeat_ok = 0;
        sva_check("sva_str_self_copy_repeated", sva_self_repeat_ok == 1);
        sva_check("sva_str_self_copy_identity",
                  sva_self_str.valid() == 1 &&
                      sva_self_str.c_str() == sva_self_ptr &&
                      sva_self_str.size() == sva_self_size &&
                      sva_self_str.capacity() == sva_self_cap);
        sva_check("sva_str_self_copy_content",
                  sva_self_str.equals("self-assignment-payload") == 1);
        sva_check("sva_str_self_copy_empty",
                  sva_empty_self_str.copy_from(&sva_empty_self_str) == 0 &&
                      sva_empty_self_str.valid() == 1 &&
                      sva_empty_self_str.size() == 0 &&
                      sva_empty_self_str.equals("") == 1);
    }

    /* ------------------------------------------------------------------ B */
    {
        string sva_grow_str = string("seed");
        const char *sva_chunk = "-append-chunk-0123456789"; /* 24 bytes */
        int sva_grow_i = 0;
        int sva_grow_ok = 1;
        size_t sva_grow_expected = 4u + 200u * strlen(sva_chunk);
        char sva_grow_buf[64];

        for (sva_grow_i = 0; sva_grow_i < 200; sva_grow_i++)
            if (sva_grow_str.append(sva_chunk) != 0)
                sva_grow_ok = 0;
        sva_check("sva_str_append_growth_rc", sva_grow_ok == 1);
        sva_check("sva_str_append_growth_size",
                  sva_grow_str.size() == sva_grow_expected);
        sva_check("sva_str_append_growth_valid",
                  sva_grow_str.valid() == 1 &&
                      sva_grow_str.capacity() >= sva_grow_str.size());
        sva_check("sva_str_append_growth_terminator",
                  sva_grow_str.c_str()[sva_grow_str.size()] == '\0');
        sva_check("sva_str_append_growth_content",
                  sva_grow_str.substr(0, 4, sva_grow_buf,
                                      sizeof(sva_grow_buf)) == 0 &&
                      strcmp(sva_grow_buf, "seed") == 0 &&
                      sva_grow_str.substr(4, strlen(sva_chunk), sva_grow_buf,
                                          sizeof(sva_grow_buf)) == 0 &&
                      strcmp(sva_grow_buf, sva_chunk) == 0 &&
                      sva_grow_str.substr(sva_grow_expected - strlen(sva_chunk),
                                          strlen(sva_chunk), sva_grow_buf,
                                          sizeof(sva_grow_buf)) == 0 &&
                      strcmp(sva_grow_buf, sva_chunk) == 0 &&
                      sva_grow_str.find(sva_chunk) == 4);

        /* Self-append: doubling through the owning buffer (alias path). */
        {
            string sva_self_append = string("abc");
            int sva_sa_ok = 1;
            if (sva_self_append.append(sva_self_append.c_str()) != 0)
                sva_sa_ok = 0;
            if (sva_self_append.append(sva_self_append.c_str()) != 0)
                sva_sa_ok = 0;
            if (sva_self_append.append(sva_self_append.c_str()) != 0)
                sva_sa_ok = 0;
            sva_check("sva_str_self_append_growth",
                      sva_sa_ok == 1 && sva_self_append.size() == 24);
            sva_check("sva_str_self_append_content",
                      sva_self_append.equals("abcabcabcabcabcabcabcabc") == 1);
        }
        /* Suffix of self: overlapping alias with a non-zero source offset. */
        {
            string sva_suffix_append = string("0123456789");
            sva_check("sva_str_suffix_append",
                      sva_suffix_append.append(sva_suffix_append.c_str() + 4) ==
                              0 &&
                          sva_suffix_append.equals("0123456789456789") == 1);
        }
    }

    /* ------------------------------------------------------------------ C */
    {
        string_view sva_nul_view = string_view("a\0b\0c", 5);
        string sva_nul_limited = string("a\0b");

        sva_check("sva_nul_view_size", sva_nul_view.size() == 5);
        sva_check("sva_nul_view_utf8", sva_nul_view.is_valid_utf8() == 1);
        sva_check("sva_nul_validator",
                  stdlib_utf8_validate("a\0b\0c", 5) == 1);
        /* SKIP (documented in NOTES): `string` storage is strlen-based, so an
         * embedded NUL cannot be stored; this check records the limitation. */
        sva_check("sva_skip_str_nul_storage",
                  sva_nul_limited.size() == 1 &&
                      sva_nul_limited.equals("a") == 1);
        sva_check("sva_nul_view_equals_limited",
                  sva_nul_view.equals("a\0b") == 0);
    }

    /* ------------------------------------------------------------------ D */
    {
        string sva_view_owner = string("view-owner-content");
        size_t sva_vo_len = sva_view_owner.size();
        const char *sva_vo_base = sva_view_owner.c_str();
        const char *sva_vo_end_ptr = sva_vo_base + sva_vo_len;
        const char *sva_vo_beyond_ptr = sva_vo_base + sva_vo_len + 1;
        string_view sva_view_at_end = string_view(sva_vo_end_ptr, 0);
        string_view sva_view_beyond = string_view(sva_vo_beyond_ptr, 0);
        string_view sva_view_snapshot = sva_view_owner.to_view();
        const char *sva_snapshot_ptr = sva_view_snapshot.data();
        size_t sva_snapshot_size = sva_view_snapshot.size();
        unsigned long long sva_snapshot_gen =
            stdlib_alloc_generation(sva_snapshot_ptr);

        /* Pointer + size only; neither view is dereferenced. */
        sva_check("sva_view_at_end_ptr",
                  sva_view_at_end.data() == sva_vo_end_ptr);
        sva_check("sva_view_at_end_size", sva_view_at_end.size() == 0);
        sva_check("sva_view_beyond_ptr",
                  sva_view_beyond.data() == sva_vo_beyond_ptr);
        sva_check("sva_view_beyond_size", sva_view_beyond.size() == 0);
        sva_check("sva_view_snapshot_setup",
                  sva_snapshot_ptr == sva_vo_base &&
                      sva_snapshot_size == sva_vo_len &&
                      sva_snapshot_gen != 0ULL);

        sva_view_owner.clear();
        sva_check("sva_view_clear_ptr_stable",
                  sva_view_snapshot.data() == sva_snapshot_ptr);
        sva_check("sva_view_clear_size_snapshot",
                  sva_view_snapshot.size() == sva_snapshot_size);
        sva_check("sva_view_clear_owner_empty",
                  sva_view_owner.size() == 0 &&
                      sva_view_owner.c_str() == sva_snapshot_ptr);

        sva_check("sva_view_realloc_rc",
                  sva_view_owner.reserve(1024) == 0 &&
                      sva_view_owner.capacity() >= 1024);
        sva_check("sva_view_realloc_moved",
                  sva_view_owner.c_str() != sva_snapshot_ptr);
        sva_check("sva_view_realloc_ptr_stale",
                  sva_view_snapshot.data() == sva_snapshot_ptr);
        sva_check("sva_view_realloc_size_snapshot",
                  sva_view_snapshot.size() == sva_snapshot_size);
        /* Allocator metadata, not a dereference of the stale view. */
        sva_check("sva_view_realloc_old_generation_stale",
                  stdlib_alloc_check_generation(sva_snapshot_ptr,
                                                sva_snapshot_gen) == 0);
    }

    /* ------------------------------------------------------------------ E */
    {
        string sva_utf8_ok = string("\xc3\xa9\xe2\x82\xac"); /* e-acute, euro */
        string sva_utf8_bad = string("\xc3\x28");            /* stray cont */
        string sva_utf8_empty = string("");
        string_view sva_utf8_ok_view = sva_utf8_ok.to_view();
        string_view sva_utf8_bad_view = sva_utf8_bad.to_view();
        int sva_utf8_i = 0;
        int sva_utf8_ok_stable = 1;
        int sva_utf8_bad_stable = 1;
        int sva_utf8_view_ok_stable = 1;
        int sva_utf8_view_bad_stable = 1;

        for (sva_utf8_i = 0; sva_utf8_i < 16; sva_utf8_i++) {
            if (sva_utf8_ok.is_valid_utf8() != 1)
                sva_utf8_ok_stable = 0;
            if (sva_utf8_bad.is_valid_utf8() != 0)
                sva_utf8_bad_stable = 0;
            if (sva_utf8_ok_view.is_valid_utf8() != 1)
                sva_utf8_view_ok_stable = 0;
            if (sva_utf8_bad_view.is_valid_utf8() != 0)
                sva_utf8_view_bad_stable = 0;
        }
        sva_check("sva_str_utf8_valid_repeat", sva_utf8_ok_stable == 1);
        sva_check("sva_str_utf8_invalid_repeat", sva_utf8_bad_stable == 1);
        sva_check("sva_view_utf8_valid_repeat", sva_utf8_view_ok_stable == 1);
        sva_check("sva_view_utf8_invalid_repeat", sva_utf8_view_bad_stable == 1);
        sva_check("sva_str_utf8_empty", sva_utf8_empty.is_valid_utf8() == 1);
        sva_check("sva_utf8_validator_edges",
                  stdlib_utf8_validate("\xc0\x80", 2) == 0 &&
                      stdlib_utf8_validate("\xed\xa0\x80", 3) == 0 &&
                      stdlib_utf8_validate("\xf4\x90\x80\x80", 4) == 0 &&
                      stdlib_utf8_validate("\xf4\x8f\xbf\xbf", 4) == 1 &&
                      stdlib_utf8_validate(NULL, 0) == 1 &&
                      stdlib_utf8_validate(NULL, 3) == 0);
    }

    /* ------------------------------------------------------------------ F */
    {
        vector sva_self_vec = vector();
        int sva_sv_i = 0;
        int sva_sv_ok = 1;
        int sva_sv_repeat_ok = 1;
        int sva_sv_out = -1;
        vector sva_empty_self_vec = vector();
        slice sva_empty_self_slice;

        for (sva_sv_i = 0; sva_sv_i < 16; sva_sv_i++)
            if (sva_self_vec.push_back(sva_sv_i * 7 + 3) != 0)
                sva_sv_ok = 0;
        sva_check("sva_vec_self_copy_setup",
                  sva_sv_ok == 1 && sva_self_vec.size() == 16);
        sva_check("sva_vec_self_copy_rc",
                  sva_self_vec.copy_from(&sva_self_vec) == 0);
        sva_check("sva_vec_self_copy_shape",
                  sva_self_vec.size() == 16 && sva_self_vec.capacity() >= 16);
        if (sva_self_vec.at_checked(0, &sva_sv_out) == 0)
            sva_check("sva_vec_self_copy_first", sva_sv_out == 3);
        else
            sva_check("sva_vec_self_copy_first", 0);
        if (sva_self_vec.at_checked(15, &sva_sv_out) == 0)
            sva_check("sva_vec_self_copy_last", sva_sv_out == 15 * 7 + 3);
        else
            sva_check("sva_vec_self_copy_last", 0);
        for (sva_sv_i = 0; sva_sv_i < 8; sva_sv_i++)
            if (sva_self_vec.copy_from(&sva_self_vec) != 0)
                sva_sv_repeat_ok = 0;
        sva_check("sva_vec_self_copy_repeated",
                  sva_sv_repeat_ok == 1 && sva_self_vec.size() == 16);
        sva_empty_self_slice = sva_empty_self_vec.view();
        sva_check("sva_vec_self_copy_empty",
                  sva_empty_self_vec.copy_from(&sva_empty_self_vec) == 0 &&
                      sva_empty_self_vec.size() == 0 &&
                      sva_empty_self_vec.capacity() == 0 &&
                      sva_empty_self_slice.size() == 0);
    }

    /* ------------------------------------------------------------------ G */
    {
        vector sva_ops_vec = vector();
        int sva_shadow[1000];
        int sva_shadow_n = 0;
        int sva_ops_i = 0;
        unsigned int sva_ops_r = 0u;
        int sva_ops_push_val = 0;
        int sva_ops_pop_val = 0;
        int sva_ops_ok = 1;
        int sva_ops_pops = 0;
        int sva_ops_pushes = 0;

        sva_lcg_reseed(0xA2C1E3u);
        for (sva_ops_i = 0; sva_ops_i < 1000; sva_ops_i++) {
            sva_ops_r = sva_lcg_next();
            if (sva_shadow_n == 0 || (sva_ops_r & 3u) != 0) {
                sva_ops_push_val = (int)((sva_ops_r >> 8) & 0x7FFFFFFFu);
                if (sva_shadow_n >= 1000) {
                    sva_ops_ok = 0;
                    break;
                }
                if (sva_ops_vec.push_back(sva_ops_push_val) != 0) {
                    sva_ops_ok = 0;
                    break;
                }
                sva_shadow[sva_shadow_n++] = sva_ops_push_val;
                sva_ops_pushes++;
            } else {
                int sva_top = -1;
                size_t sva_top_index = (size_t)sva_shadow_n;
                if (sva_shadow_n > 0) {
                    sva_top_index--;
                    sva_top = sva_shadow[sva_top_index];
                }
                if (sva_ops_vec.pop_back(&sva_ops_pop_val) != 0) {
                    sva_ops_ok = 0;
                    break;
                }
                if (sva_top < 0 || sva_ops_pop_val != sva_top)
                    sva_ops_ok = 0;
                sva_shadow_n--;
                sva_ops_pops++;
            }
            if ((int)sva_ops_vec.size() != sva_shadow_n)
                sva_ops_ok = 0;
        }
        sva_check("sva_vec_pushpop_ops", sva_ops_ok == 1);
        sva_check("sva_vec_pushpop_count",
                  sva_ops_pops > 0 && sva_shadow_n > 0 &&
                      sva_ops_pushes + sva_ops_pops == 1000 &&
                      sva_ops_pushes == sva_ops_pops + sva_shadow_n);
        {
            int sva_final_i = 0;
            int sva_final_out = 0;
            int sva_final_ok = 1;
            for (sva_final_i = 0; sva_final_i < sva_shadow_n; sva_final_i++) {
                if (sva_ops_vec.at_checked((size_t)sva_final_i,
                                           &sva_final_out) != 0 ||
                    sva_final_out != sva_shadow[sva_final_i])
                    sva_final_ok = 0;
            }
            sva_check("sva_vec_pushpop_final", sva_final_ok == 1);
        }
    }

    /* ------------------------------------------------------------------ H */
    {
        vector sva_slice_vec = vector();
        slice sva_slice_clear;
        slice sva_slice_erase;
        int sva_slice_i = 0;
        int sva_slice_out = -1;

        for (sva_slice_i = 0; sva_slice_i < 8; sva_slice_i++)
            if (sva_slice_vec.push_back(sva_slice_i + 100) != 0)
                sva_slice_out = -2;
        sva_slice_clear = sva_slice_vec.view();
        sva_check("sva_vec_slice_clear_fresh",
                  sva_slice_clear.stale() == 0 &&
                      sva_slice_clear.valid() == 1 &&
                      sva_slice_clear.size() == 8);
        sva_check("sva_vec_slice_clear_read",
                  sva_slice_clear.at_checked(0, &sva_slice_out) == 0 &&
                      sva_slice_out == 100);
        sva_slice_vec.clear();
        sva_check("sva_vec_slice_clear_stale",
                  sva_slice_clear.stale() == 1 &&
                      sva_slice_clear.valid() == 0);
        sva_check("sva_vec_slice_clear_refused",
                  sva_slice_clear.at_checked(0, &sva_slice_out) == -1);

        for (sva_slice_i = 0; sva_slice_i < 8; sva_slice_i++)
            if (sva_slice_vec.push_back(sva_slice_i + 200) != 0)
                sva_slice_out = -2;
        sva_slice_erase = sva_slice_vec.view();
        sva_check("sva_vec_slice_erase_fresh",
                  sva_slice_erase.stale() == 0 &&
                      sva_slice_erase.size() == 8);
        sva_check("sva_vec_slice_erase_rc",
                  sva_slice_vec.erase(2, &sva_slice_out) == 0 &&
                      sva_slice_out == 202);
        sva_check("sva_vec_slice_erase_stale",
                  sva_slice_erase.stale() == 1 &&
                      sva_slice_erase.valid() == 0);
        sva_check("sva_vec_slice_erase_refused",
                  sva_slice_erase.at_checked(2, &sva_slice_out) == -1);
    }

    /* ------------------------------------------------------------------ I */
    {
        vector sva_empty_vec = vector();
        slice sva_empty_slice = sva_empty_vec.view();
        slice sva_empty_sub = sva_empty_slice.sub(0, 4);
        int sva_empty_out = -1;

        sva_check("sva_vec_empty_view_size",
                  sva_empty_slice.size() == 0 && sva_empty_vec.size() == 0);
        sva_check("sva_vec_empty_view_valid",
                  sva_empty_slice.valid() == 0 &&
                      sva_empty_slice.stale() == 0);
        sva_check("sva_vec_empty_view_read_refused",
                  sva_empty_slice.at_checked(0, &sva_empty_out) == -1);
        sva_check("sva_vec_empty_view_sub",
                  sva_empty_sub.size() == 0 && sva_empty_sub.valid() == 0);
        sva_check("sva_vec_empty_view_stale_after_push",
                  sva_empty_vec.push_back(7) == 0 &&
                      sva_empty_slice.stale() == 1);
    }

    sva_final_rc = sva_failures == 0 ? 0 : 1;
    print("EXTRA_STRING_VECTOR_ALIAS", sva_checks, sva_failures);
    return sva_final_rc;
}
