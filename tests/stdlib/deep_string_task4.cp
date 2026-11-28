/* Deep Task-4 contract tests for stdlib/string.hp (uncovered cases only).
 *
 * This fixture covers ONLY the cases in the parent agent's Task-4 list that are
 * absent from tests/stdlib/deep_string_append.cp (append/reserve/drop/fault
 * probes, fatal forks) and tests/stdlib/deep_string_view.cp (borrowed-view
 * lifetime, generation-based invalidation).  See
 * docs/stdlib/DEEP_STRING_TASK4_CONTRACT.md for the exact coverage map,
 * expected stdout and DOCUMENTED vs UNDOCUMENTED classification.
 *
 *  1. constructor allocation failure + all three re-init paths        (ctor_*)
 *  2. invalid-object read behaviour                                   (invalid_*)
 *  3. copy_from: NULL/self/overwrite, single-point atomicity, telemetry (copy_*)
 *  4. exact capacity boundaries with allocation-sequence deltas       (cap_*)
 *  5. repeated drop (x3) and use-after-drop                           (drop3_*)
 *  6. integer overflow with STDLIB_ALLOC_LIMIT=4096, no allocation    (ovf_*)
 *  7. substr / field boundaries                                       (sub_*, field_*)
 *  8. string_view: NULL, explicit UTF-8 bytes, accessors, stale view  (view_*)
 *
 * STDLIB_ALLOC_LIMIT is defined to 4096 BEFORE including error.hp.  This is the
 * allocator's documented build-time override (it is #ifndef guarded in
 * error.hp) and lets the overflow branches be reached with small inputs; it
 * does NOT touch string.hp and never allocates near the real 1 TiB limit.
 *
 * STALE-VIEW RULE: after a reallocating reserve() the borrowed view's data()
 * pointer is captured and compared only as an integer/pointer value.  Its
 * equals()/is_valid_utf8() are NEVER called on the stale view: string_view has
 * no generation tracking, so detecting invalidation is the caller's job.
 *
 * Output: one "<tag> <0|1>" per check, then "STRING_TASK4 <checks> <failures>".
 * Exit code is 0 only when failures == 0.  No .expected file (file ownership is
 * restricted); exact stdout is pinned in the contract document.
 */
#define STDLIB_ALLOC_LIMIT ((size_t)4096)
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

static int t4_checks = 0;
static int t4_failures = 0;

static void t4_ck(int cond, const char *tag) {
    t4_checks++;
    if (!cond)
        t4_failures++;
    print(tag, cond ? 1 : 0);
}

/* ------------------------------------------------ 1. + 2. ctor failure/invalid */
static void t4_ctor_and_invalid(void) {
    string t4_bad;
    string t4_ctor_src;
    string t4_inv;

    /* --- 1. constructor allocation failure ------------------------------- */
    stdlib_alloc_fail_at(1);
    t4_bad = string("abc");                 /* single ctor allocation fails */
    stdlib_alloc_fail_reset();

    t4_ck(t4_bad.valid() == 0, "t4_ctor_allocfail_valid");
    t4_ck(t4_bad.size() == 0, "t4_ctor_allocfail_size");
    t4_ck(t4_bad.capacity() == 0, "t4_ctor_allocfail_cap");
    t4_ck(t4_bad.c_str() != (const char *)0 &&
          strcmp(t4_bad.c_str(), "") == 0, "t4_ctor_allocfail_cstr");
    t4_ck(t4_bad.at(0) == '\0', "t4_ctor_allocfail_at0");

    /* Reset and re-initialise through append. */
    t4_ck(t4_bad.append("Z") == 0 && t4_bad.valid() == 1 &&
          t4_bad.size() == 1 && strcmp(t4_bad.c_str(), "Z") == 0,
          "t4_ctor_reinit_append");

    /* Reset and re-initialise through reserve. */
    t4_bad.drop();
    t4_ck(t4_bad.valid() == 0, "t4_ctor_reinit_reserve_pre");
    t4_ck(t4_bad.reserve(5) == 0 && t4_bad.valid() == 1 &&
          t4_bad.capacity() == 5 && strcmp(t4_bad.c_str(), "") == 0,
          "t4_ctor_reinit_reserve");

    /* Reset and re-initialise through copy_from. */
    t4_bad.drop();
    t4_ctor_src = string("Q");
    t4_ck(t4_bad.copy_from(&t4_ctor_src) == 0 && t4_bad.valid() == 1 &&
          t4_bad.size() == 1 && t4_bad.capacity() == 2 &&
          strcmp(t4_bad.c_str(), "Q") == 0, "t4_ctor_reinit_copy");

    /* --- 2. invalid-object behaviour ------------------------------------- */
    stdlib_alloc_fail_at(1);
    t4_inv = string("abc");                 /* fresh invalid object */
    stdlib_alloc_fail_reset();

    t4_ck(t4_inv.valid() == 0, "t4_invalid_valid");
    t4_ck(t4_inv.equals("") == 1, "t4_invalid_equals_empty");
    t4_ck(t4_inv.equals("x") == 0, "t4_invalid_equals_nonempty");
    t4_ck(t4_inv.compare("") == 0, "t4_invalid_compare_empty");
    t4_ck(t4_inv.compare("x") == -1, "t4_invalid_compare_nonempty");
    t4_ck(t4_inv.find("x") == -1 && t4_inv.find("") == -1,
          "t4_invalid_find");
    t4_ck(t4_inv.is_valid_utf8() == 1, "t4_invalid_utf8");

    /* drop() on an invalid object is inert. */
    t4_inv.drop();
    t4_ck(t4_inv.valid() == 0 && t4_inv.size() == 0 &&
          t4_inv.capacity() == 0 && t4_inv.c_str() != (const char *)0 &&
          strcmp(t4_inv.c_str(), "") == 0, "t4_invalid_drop_inert");
}

/* ------------------------------------------------------------ 3. copy_from */
static void t4_copy_from(void) {
    string t4_dst;
    string t4_short;
    string t4_long;
    const char *t4_before;

    t4_dst = string("original");
    t4_short = string("s");
    t4_long = string("a-much-longer-replacement");
    t4_before = t4_dst.c_str();

    /* NULL source: -1, byte-for-byte unchanged. */
    t4_ck(t4_dst.copy_from(NULL) == -1, "t4_copy_null_rc");
    t4_ck(t4_dst.size() == 8 && t4_dst.capacity() == 9 &&
          t4_dst.c_str() == t4_before &&
          strcmp(t4_dst.c_str(), "original") == 0, "t4_copy_null_unchanged");

    /* Self-copy: 0, unchanged. */
    t4_ck(t4_dst.copy_from(&t4_dst) == 0, "t4_copy_self_rc");
    t4_ck(t4_dst.size() == 8 && t4_dst.capacity() == 9 &&
          t4_dst.c_str() == t4_before &&
          strcmp(t4_dst.c_str(), "original") == 0, "t4_copy_self_unchanged");

    /* Overwrite with a shorter source. */
    t4_ck(t4_dst.copy_from(&t4_short) == 0, "t4_copy_shorter_rc");
    t4_ck(t4_dst.size() == 1 && t4_dst.capacity() == 2 &&
          strcmp(t4_dst.c_str(), "s") == 0, "t4_copy_shorter_state");

    /* Overwrite with a longer source. */
    t4_ck(t4_dst.copy_from(&t4_long) == 0, "t4_copy_longer_rc");
    t4_ck(t4_dst.size() == 25 && t4_dst.capacity() == 26 &&
          strcmp(t4_dst.c_str(), "a-much-longer-replacement") == 0,
          "t4_copy_longer_state");

    /* Failure atomicity at the single allocation point. */
    t4_before = t4_dst.c_str();
    stdlib_alloc_fail_at(1);
    t4_ck(t4_dst.copy_from(&t4_short) == -1, "t4_copy_fail_rc");
    stdlib_alloc_fail_reset();
    t4_ck(t4_dst.size() == 25 && t4_dst.capacity() == 26 &&
          t4_dst.c_str() == t4_before &&
          strcmp(t4_dst.c_str(), "a-much-longer-replacement") == 0,
          "t4_copy_fail_unchanged");

    /* Retry succeeds. */
    t4_ck(t4_dst.copy_from(&t4_short) == 0 && t4_dst.valid() == 1 &&
          t4_dst.size() == 1 && strcmp(t4_dst.c_str(), "s") == 0,
          "t4_copy_retry");
}

/* Telemetry in its own scope: every allocation made inside the window is
 * released before the snapshot, and the explicit drops make destructors no-ops. */
static void t4_copy_telemetry(void) {
    string t4_ta;
    string t4_tb;
    stdlib_telemetry_stats t4_tm;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    /* Construct INSIDE the window so every counted allocation is also freed
     * inside it; constructing before reset would free uncounted buffers here. */
    t4_ta = string("one");
    t4_tb = string("two");
    t4_ta.copy_from(&t4_tb);
    t4_tb.copy_from(&t4_ta);
    t4_ta.drop();
    t4_tb.drop();
    stdlib_telemetry_snapshot(&t4_tm);
    stdlib_telemetry_set_enabled(0);

    t4_ck(t4_tm.alloc_count > 0ULL && t4_tm.free_count > 0ULL,
          "t4_copy_telemetry_activity");
    t4_ck(t4_tm.alloc_count == t4_tm.free_count,
          "t4_copy_telemetry_balanced");
}

/* ------------------------------------------------ 4. exact capacity boundaries */
static void t4_capacity(void) {
    string t4_cap;
    long t4_seq0;
    long t4_seq1;

    t4_cap = string("abc");                 /* count 3, space 4 */
    t4_ck(t4_cap.capacity() == 4, "t4_cap_initial");

    /* reserve(n) with n == capacity: no allocation at all. */
    t4_seq0 = stdlib_alloc_seq();
    t4_ck(t4_cap.reserve(4) == 0, "t4_cap_at_rc");
    t4_seq1 = stdlib_alloc_seq();
    t4_ck(t4_seq1 - t4_seq0 == 0, "t4_cap_at_noop_seq");
    t4_ck(t4_cap.capacity() == 4 && strcmp(t4_cap.c_str(), "abc") == 0,
          "t4_cap_at_noop_state");

    /* reserve(capacity+1): exactly one allocation, capacity == n. */
    t4_seq0 = stdlib_alloc_seq();
    t4_ck(t4_cap.reserve(5) == 0, "t4_cap_plusone_rc");
    t4_seq1 = stdlib_alloc_seq();
    t4_ck(t4_seq1 - t4_seq0 == 1, "t4_cap_plusone_seq");
    t4_ck(t4_cap.capacity() == 5 && strcmp(t4_cap.c_str(), "abc") == 0,
          "t4_cap_plusone_state");

    /* Append that exactly fills capacity: need == space, no allocation. */
    t4_ck(t4_cap.reserve(8) == 0 && t4_cap.capacity() == 8,
          "t4_cap_fill_setup");
    t4_seq0 = stdlib_alloc_seq();
    t4_ck(t4_cap.append("1234") == 0, "t4_cap_fill_rc");
    t4_seq1 = stdlib_alloc_seq();
    t4_ck(t4_seq1 - t4_seq0 == 0, "t4_cap_fill_seq");
    t4_ck(t4_cap.size() == 7 && t4_cap.capacity() == 8 &&
          strcmp(t4_cap.c_str(), "abc1234") == 0, "t4_cap_fill_state");

    /* Append needing exactly one more byte: one allocation, capacity doubles. */
    t4_seq0 = stdlib_alloc_seq();
    t4_ck(t4_cap.append("5") == 0, "t4_cap_onemore_rc");
    t4_seq1 = stdlib_alloc_seq();
    t4_ck(t4_seq1 - t4_seq0 == 1, "t4_cap_onemore_seq");
    t4_ck(t4_cap.size() == 8 && t4_cap.capacity() == 16 &&
          strcmp(t4_cap.c_str(), "abc12345") == 0, "t4_cap_onemore_state");
}

/* ---------------------------------------------------------- 5. repeated drop */
static void t4_repeated_drop(void) {
    string t4_drop;
    string t4_drop_src;

    t4_drop = string("xyz");
    t4_drop.drop();
    t4_drop.drop();
    t4_drop.drop();

    t4_ck(t4_drop.valid() == 0 && t4_drop.size() == 0 &&
          t4_drop.capacity() == 0, "t4_drop3_state");
    t4_ck(t4_drop.c_str() != (const char *)0, "t4_drop3_cstr_nonnull");
    t4_ck(strcmp(t4_drop.c_str(), "") == 0, "t4_drop3_cstr_empty");
    t4_ck(t4_drop.at(0) == '\0', "t4_drop3_at0");

    /* Use after drop via append. */
    t4_ck(t4_drop.append("A") == 0 && t4_drop.valid() == 1 &&
          strcmp(t4_drop.c_str(), "A") == 0, "t4_drop3_reinit_append");

    /* Use after drop via reserve. */
    t4_drop.drop();
    t4_ck(t4_drop.reserve(3) == 0 && t4_drop.valid() == 1 &&
          t4_drop.capacity() == 3 && strcmp(t4_drop.c_str(), "") == 0,
          "t4_drop3_reinit_reserve");

    /* Use after drop via copy_from. */
    t4_drop.drop();
    t4_drop_src = string("rep");
    t4_ck(t4_drop.copy_from(&t4_drop_src) == 0 && t4_drop.valid() == 1 &&
          strcmp(t4_drop.c_str(), "rep") == 0, "t4_drop3_reinit_copy");
}

/* --------------------------------------------------------- 6. integer overflow */
static void t4_overflow(void) {
    string t4_ovf;
    string t4_src;
    char t4_big[3000];
    const char *t4_before;
    long t4_seq0;
    long t4_seq1;

    memset(t4_big, 'b', sizeof(t4_big));
    t4_big[sizeof(t4_big) - 1] = '\0';

    t4_ovf = string("seed");                 /* count 4, space 5 */
    t4_before = t4_ovf.c_str();

    /* reserve(LIMIT+1): -1 with NO allocation attempt and unchanged state. */
    t4_seq0 = stdlib_alloc_seq();
    t4_ck(t4_ovf.reserve(STDLIB_ALLOC_LIMIT + 1) == -1, "t4_ovf_reserve_rc");
    t4_seq1 = stdlib_alloc_seq();
    t4_ck(t4_seq1 - t4_seq0 == 0, "t4_ovf_reserve_noalloc");
    t4_ck(t4_ovf.size() == 4 && t4_ovf.capacity() == 5 &&
          t4_ovf.c_str() == t4_before && strcmp(t4_ovf.c_str(), "seed") == 0,
          "t4_ovf_reserve_unchanged");

    /* Overflowing append: source length makes result exceed the limit. */
    t4_src = string(t4_big);                  /* count 2999, fits in 4096 */
    t4_ck(t4_src.valid() == 1 && t4_src.size() == 2999,
          "t4_ovf_source_built");
    t4_seq0 = stdlib_alloc_seq();
    t4_ck(t4_src.append(t4_big) == -1, "t4_ovf_append_rc");
    t4_seq1 = stdlib_alloc_seq();
    t4_ck(t4_seq1 - t4_seq0 == 0, "t4_ovf_append_noalloc");
    t4_ck(t4_src.valid() == 1 && t4_src.size() == 2999 &&
          t4_src.at(0) == 'b' && t4_src.at(2998) == 'b' &&
          strcmp(t4_src.c_str(), t4_big) == 0, "t4_ovf_append_unchanged");
}

/* ------------------------------------------------------ 7. substr / field */
static void t4_substr_field(void) {
    string t4_s;
    string t4_first;
    string t4_last;
    string t4_trunc;
    char t4_out[16];

    t4_s = string("a,b,c");                  /* count 5 */
    t4_first = string(",lead");
    t4_last = string("tail,");
    t4_trunc = string("abcdef,ghi");

    /* Output-buffer guards. */
    t4_ck(t4_s.substr(0, 2, (char *)0, sizeof(t4_out)) == -1,
          "t4_sub_null_out");
    t4_ck(t4_s.field(',', 0, (char *)0, sizeof(t4_out)) == -1,
          "t4_field_null_out");
    t4_ck(t4_s.substr(0, 2, t4_out, 0) == -1, "t4_sub_zero_outsize");
    t4_ck(t4_s.field(',', 0, t4_out, 0) == -1, "t4_field_zero_outsize");

    /* start > size is rejected; start == size is an empty substring. */
    t4_ck(t4_s.substr(6, 2, t4_out, sizeof(t4_out)) == -1,
          "t4_sub_start_gt_size");
    t4_ck(t4_s.substr(5, 2, t4_out, sizeof(t4_out)) == 0 &&
          strcmp(t4_out, "") == 0, "t4_sub_start_eq_size");

    /* n > available clamps; exact fit; small out_size truncates. */
    t4_ck(t4_s.substr(1, 100, t4_out, sizeof(t4_out)) == 0 &&
          strcmp(t4_out, ",b,c") == 0, "t4_sub_clamp");
    t4_ck(t4_s.substr(0, 5, t4_out, 6) == 0 &&
          strcmp(t4_out, "a,b,c") == 0, "t4_sub_exact");
    /* out_size 3 -> at most 2 bytes copied: "a," (the first two of "a,b,c"). */
    t4_ck(t4_s.substr(0, 5, t4_out, 3) == 0 &&
          strcmp(t4_out, "a,") == 0, "t4_sub_truncate");

    /* Separator at the first position. */
    t4_ck(t4_first.count_fields(',') == 2, "t4_field_sep_first_count");
    t4_ck(t4_first.field(',', 0, t4_out, sizeof(t4_out)) == 0 &&
          strcmp(t4_out, "") == 0, "t4_field_sep_first_0");
    t4_ck(t4_first.field(',', 1, t4_out, sizeof(t4_out)) == 0 &&
          strcmp(t4_out, "lead") == 0, "t4_field_sep_first_1");

    /* Separator at the last position. */
    t4_ck(t4_last.count_fields(',') == 2, "t4_field_sep_last_count");
    t4_ck(t4_last.field(',', 0, t4_out, sizeof(t4_out)) == 0 &&
          strcmp(t4_out, "tail") == 0, "t4_field_sep_last_0");
    t4_ck(t4_last.field(',', 1, t4_out, sizeof(t4_out)) == 0 &&
          strcmp(t4_out, "") == 0, "t4_field_sep_last_1");

    /* Index out of range; field output truncation. */
    t4_ck(t4_s.field(',', 5, t4_out, sizeof(t4_out)) == -1,
          "t4_field_index_oor");
    t4_ck(t4_trunc.field(',', 0, t4_out, 3) == 0 &&
          strcmp(t4_out, "ab") == 0, "t4_field_truncate");
}

/* ------------------------------------------------------------ 8. string_view */
static void t4_view(void) {
    string t4_owner;
    string_view t4_view;
    string_view t4_empty;
    string_view t4_valid;
    string_view t4_badview;
    const char t4_valid_bytes[] = "\xc3\xa9\xe2\x82\xac"; /* 5 bytes + NUL */
    const char t4_bad_bytes[] = "\xc3\x28";               /* 2 bytes + NUL */
    const char *t4_stale;
    size_t t4_saved_size;
    size_t t4_saved_size2;

    t4_owner = string("abcdef");             /* count 6, space 7 */
    t4_view = t4_owner.to_view();
    t4_ck(t4_view.size() == 6, "t4_view_size");
    t4_ck(t4_view.data() == t4_owner.c_str(), "t4_view_data_identity");
    t4_ck(t4_view.equals("abcdef") == 1, "t4_view_equals");
    t4_ck(t4_view.equals(NULL) == 0, "t4_view_equals_null");

    t4_empty = string_view(NULL, 0);
    t4_ck(t4_empty.equals(NULL) == 0, "t4_view_empty_equals_null");

    /* is_valid_utf8 over explicit byte arrays with explicit lengths. */
    t4_valid = string_view(t4_valid_bytes, 5);
    t4_ck(t4_valid.size() == 5 && t4_valid.data() == t4_valid_bytes &&
          t4_valid.is_valid_utf8() == 1, "t4_view_utf8_valid_bytes");
    t4_badview = string_view(t4_bad_bytes, 2);
    t4_ck(t4_badview.size() == 2 && t4_badview.data() == t4_bad_bytes &&
          t4_badview.is_valid_utf8() == 0, "t4_view_utf8_invalid_bytes");

    /* Stale-view safety: force reallocation with reserve(), then compare the
     * captured pointer/size ONLY.  Never call equals()/is_valid_utf8() on the
     * stale view: string_view has no generation tracking (caller's duty). */
    t4_stale = t4_view.data();
    t4_saved_size = t4_view.size();
    t4_saved_size2 = t4_saved_size;
    t4_ck(t4_owner.reserve(64) == 0 && t4_owner.capacity() == 64,
          "t4_view_stale_reserve_rc");
    t4_ck(t4_owner.c_str() != t4_stale, "t4_view_stale_pointer_moved");
    t4_ck(t4_view.data() == (const char *)0, "t4_view_stale_data_refused");
    t4_ck(t4_saved_size2 == 6 && t4_view.size() == t4_saved_size,
          "t4_view_stale_size_unchanged");
}

int main(void) {
    t4_ctor_and_invalid();
    t4_copy_from();
    t4_copy_telemetry();
    t4_capacity();
    t4_repeated_drop();
    t4_overflow();
    t4_substr_field();
    t4_view();
    print("STRING_TASK4", t4_checks, t4_failures);
    return t4_failures == 0 ? 0 : 1;
}
