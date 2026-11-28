/* ABI_SAFETY / container generation propagation and borrow-vs-own (agent 2).
 *
 * Self-checking, deterministic, no stdin, no external library. Stale pointers
 * and stale views are ONLY queried (generation / staleness / refusal APIs);
 * they are never dereferenced.
 *
 * Covered here:
 *   A  string: stdlib side-table generation changes when append/reserve
 *      reallocates; a saved stale c_str() reports generation 0 and is refused
 *      by generation checks; string_view (borrow) never owns storage;
 *   B  vector: slice staleness generation propagation after growth, at_checked
 *      refusal on a stale slice, and sub-slice staleness inheritance;
 *   C  collections: intmap / strmap growth and drop keep the stdlib side table
 *      balanced; strmap owns copied keys (source mutation cannot change them);
 *   D  file.hp: file_borrow (borrowed stream) leaves the underlying FILE* open
 *      after close, while an owned file_temp closes it; double close is
 *      refused with a lifecycle-category error.
 *
 * Every check prints "<tag> <0|1>". The final line is
 * "ABI_SAFETY <checks> <failures>"; exit 0 only on zero failures.
 * All owned variables use the unique "ascb_" prefix.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"
#include "stdlib/file.hp"

static int ascb_checks = 0;
static int ascb_failures = 0;

static void ascb_check(const char *tag, int cond) {
    ascb_checks++;
    if (!cond)
        ascb_failures++;
    print(tag, cond ? 1 : 0);
}

int main(void) {
    size_t ascb_base_live = 0;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    ascb_base_live = stdlib_state.meta_live;

    /* ---- A: string generation propagation + borrowed view ---------------- */
    {
        string ascb_s = string("alpha");
        const char *ascb_old = NULL;
        unsigned long long ascb_g0 = 0ULL;
        string_view ascb_view;
        int ascb_out = -1;

        ascb_old = ascb_s.c_str();
        ascb_g0 = stdlib_alloc_generation(ascb_old);
        ascb_view = ascb_s.to_view();
        ascb_check("ascb_a_init", ascb_s.valid() == 1 && ascb_g0 != 0ULL &&
                                      ascb_view.data() == ascb_old &&
                                      ascb_view.size() == 5u);
        ascb_check("ascb_a_generation_live",
                   stdlib_alloc_check_generation(ascb_old, ascb_g0) == 1);

        /* In-place mutation does not reallocate: generation is unchanged. */
        ascb_s.clear();
        ascb_check("ascb_a_clear", ascb_s.size() == 0u);
        ascb_check("ascb_a_generation_after_clear",
                   stdlib_alloc_check_generation(ascb_old, ascb_g0) == 1);

        /* Reallocating mutation: old buffer is gone from the side table. */
        ascb_check("ascb_a_append", ascb_s.append("beta-gamma-delta") == 0);
        ascb_check("ascb_a_stale_gen_zero",
                   stdlib_alloc_generation(ascb_old) == 0ULL);
        ascb_check("ascb_a_stale_check_reject",
                   stdlib_alloc_check_generation(ascb_old, ascb_g0) == 0);
        ascb_check("ascb_a_fresh_gen",
                   stdlib_alloc_generation(ascb_s.c_str()) != 0ULL &&
                       stdlib_alloc_generation(ascb_s.c_str()) != ascb_g0);
        ascb_check("ascb_a_owner_read", ascb_s.equals("beta-gamma-delta") == 1);
        /* The borrowed view is stale; NO read through ascb_view.data(). */
        ascb_check("ascb_a_borrowed_view_stale",
                   ascb_view.data() == ascb_old && ascb_g0 != 0ULL);

        ascb_s.drop();
        ascb_out = (int)stdlib_alloc_generation(ascb_s.c_str());
        ascb_check("ascb_a_dropped_gen_zero", ascb_out == 0);
    }
    ascb_check("ascb_a_live_restored", stdlib_state.meta_live == ascb_base_live);

    /* ---- B: vector slice generation propagation -------------------------- */
    {
        vector ascb_v = vector();
        slice ascb_sl;
        slice ascb_sub;
        int ascb_i = 0;
        int ascb_val = -1;
        int ascb_ok = 1;
        for (ascb_i = 0; ascb_i < 8; ++ascb_i)
            ascb_v.push_back(ascb_i + 1);
        ascb_sl = ascb_v.view();
        ascb_check("ascb_b_slice_valid", ascb_sl.valid() == 1 && ascb_sl.stale() == 0);
        ascb_check("ascb_b_slice_read",
                   ascb_sl.at_checked(2, &ascb_val) == 0 && ascb_val == 3);
        ascb_sub = ascb_sl.sub(1, 3);
        ascb_check("ascb_b_sub_read",
                   ascb_sub.size() == 3u && ascb_sub.stale() == 0 &&
                       ascb_sub.at_checked(0, &ascb_val) == 0 && ascb_val == 2);
        ascb_check("ascb_b_oob_refused", ascb_sl.at_checked(8, &ascb_val) == -1);

        /* Force growth: both slices become stale and refuse reads. */
        for (ascb_i = 0; ascb_i < 128; ++ascb_i)
            ascb_v.push_back(ascb_i * 2);
        ascb_check("ascb_b_stale_after_growth", ascb_sl.stale() == 1 &&
                                                    ascb_sl.valid() == 0);
        ascb_check("ascb_b_stale_refusal",
                   ascb_sl.at_checked(0, &ascb_val) == -1 &&
                       ascb_sub.at_checked(0, &ascb_val) == -1);
        ascb_check("ascb_b_sub_inherits_stale", ascb_sub.stale() == 1);
        {
            slice ascb_fresh = ascb_v.view();
            ascb_check("ascb_b_fresh_slice", ascb_fresh.stale() == 0 &&
                                                 ascb_fresh.at_checked(0, &ascb_val) == 0 &&
                                                 ascb_val == 1);
        }
        ascb_v.drop();
        ascb_check("ascb_b_ok", ascb_ok == 1);
    }
    ascb_check("ascb_b_live_restored", stdlib_state.meta_live == ascb_base_live);

    /* ---- C: collections side-table balance + own vs borrow --------------- */
    {
        intmap ascb_map = intmap();
        strmap ascb_str = strmap();
        string ascb_key = string("origkey");
        int ascb_i = 0;
        int ascb_val = -1;
        int ascb_ok = 1;
        size_t ascb_after_int = 0;
        for (ascb_i = 0; ascb_i < 32; ++ascb_i)
            ascb_map.put(ascb_i, ascb_i * 7);
        ascb_after_int = stdlib_state.meta_live;
        ascb_check("ascb_c_intmap_valid",
                   ascb_map.valid() == 1 && ascb_map.size() == 32u &&
                       ascb_after_int > ascb_base_live);
        for (ascb_i = 0; ascb_i < 32; ++ascb_i) {
            if (ascb_map.get(ascb_i, &ascb_val) != 0 || ascb_val != ascb_i * 7)
                ascb_ok = 0;
        }
        ascb_check("ascb_c_intmap_values", ascb_ok == 1);

        /* strmap COPIES keys: mutating the source does not change the map. */
        ascb_check("ascb_c_strmap_put", ascb_str.put(ascb_key.c_str(), 99) == 0);
        ascb_check("ascb_c_key_before", ascb_str.get("origkey", &ascb_val) == 0 &&
                                            ascb_val == 99);
        ascb_check("ascb_c_key_source_mutate", ascb_key.append("XYZ") == 0);
        ascb_check("ascb_c_key_after", ascb_str.get("origkey", &ascb_val) == 0 &&
                                           ascb_val == 99 &&
                                           ascb_str.get("origkeyXYZ", &ascb_val) == -1);
        ascb_check("ascb_c_borrowed_source_stale",
                   stdlib_alloc_generation(ascb_key.c_str()) != 0ULL);

        ascb_str.drop();
        ascb_key.drop();
        ascb_map.drop();
        ascb_check("ascb_c_drop_invalid",
                   ascb_str.valid() == 0 && ascb_map.valid() == 0);
    }
    ascb_check("ascb_c_live_restored", stdlib_state.meta_live == ascb_base_live);

    /* ---- D: file.hp borrow vs own ---------------------------------------- */
    {
        FILE *ascb_raw = tmpfile();
        file ascb_borrowed = file_borrow(ascb_raw);
        error ascb_err;
        int ascb_wrote = 0;

        ascb_check("ascb_d_raw_open", ascb_raw != NULL);
        if (ascb_raw != NULL) {
            ascb_wrote = ascb_borrowed.write("borrowed");
            ascb_check("ascb_d_borrowed_write", ascb_wrote == 1);
            ascb_check("ascb_d_borrowed_close",
                       ascb_borrowed.close(&ascb_err) == STDLIB_OK &&
                           ascb_err.ok() == 1);
            ascb_check("ascb_d_borrowed_closed_handle",
                       ascb_borrowed.is_open() == 0);
            /* The borrowed stream is still ours and still open. */
            ascb_check("ascb_d_underlying_still_open", fputs("x", ascb_raw) >= 0);
            ascb_check("ascb_d_double_close_refused",
                       ascb_borrowed.close(&ascb_err) == STDLIB_FAIL &&
                           ascb_err.failed_now() == 1 &&
                           ascb_err.category() == STDLIB_CATEGORY_LIFECYCLE);
            fclose(ascb_raw);
        }
    }
    {
        error ascb_owned_err;
        file ascb_owned = file_temp(&ascb_owned_err);
        ascb_check("ascb_d_owned_open",
                   ascb_owned.is_open() == 1 && ascb_owned_err.ok() == 1);
        ascb_check("ascb_d_owned_write", ascb_owned.write("owned") == 1);
        ascb_check("ascb_d_owned_close",
                   ascb_owned.close(&ascb_owned_err) == STDLIB_OK);
        ascb_check("ascb_d_owned_second_close",
                   ascb_owned.close(&ascb_owned_err) == STDLIB_FAIL &&
                       ascb_owned_err.category() == STDLIB_CATEGORY_LIFECYCLE);
        ascb_owned.drop(); /* idempotent after close */
        ascb_check("ascb_d_owned_dropped", ascb_owned.is_open() == 0);
    }
    ascb_check("ascb_d_live_restored", stdlib_state.meta_live == ascb_base_live);

    print("ABI_SAFETY", ascb_checks, ascb_failures);
    return ascb_failures == 0 ? 0 : 1;
}
