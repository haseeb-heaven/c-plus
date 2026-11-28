/* Adversarial regression family: stale borrowed views.
 *
 * Two view kinds are exercised with STRICT metadata-only detection:
 *   vector::slice    -- generation counter owned by the vector; the library
 *                       exposes stale()/valid()/at_checked() and refuses a
 *                       read BEFORE touching the (possibly stale) item pointer.
 *   string_view      -- no generation tracking of its own; this test records
 *                       pointer + size + the checked allocator's generation
 *                       token and gates every access through a metadata-only
 *                       permit() helper.  A stale pointer is NEVER dereferenced.
 *
 * For both kinds: invalidation by owner reallocation, by shortening mutators
 * and by drop() is defined as REFUSAL; the view's recorded fields remain
 * unchanged snapshots; repeated access attempts keep refusing.
 *
 * SELF-CHECKING: every check prints "<tag> <1|0>".  The final line is
 * "ADV_STALE_VIEWS <checks> <failed>".  Exit 0 == all checks passed,
 * 1 == at least one check failed.
 *
 * NOTE ON NAMING: the C+ compiler dispatches class methods by *variable name*
 * across the whole translation unit, so every container/view instance here
 * has a globally unique name (v_live/sl_live, s_inv/sv_inv, ...).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

static int g_checks = 0;
static int g_failed = 0;

static void ck(const char *tag, int cond) {
    g_checks++;
    if (!cond)
        g_failed++;
    print(tag, cond ? 1 : 0);
}

/* Metadata-only gate for a string_view.  It consults ONLY the recorded
 * pointer, size and allocator generation token.  It never dereferences the
 * pointer, so it is safe to call any number of times after invalidation. */
static int view_permit(const char *p, size_t n, unsigned long long gen) {
    if (p == (const char *)0 || n == 0 || gen == 0ULL)
        return 0;
    if (stdlib_alloc_check_generation(p, gen) != 1)
        return 0;
    if (stdlib_alloc_generation(p) != gen)
        return 0;
    return 1;
}

/* ---------------------------------------------------- slice: live behaviour */
static void test_slice_live(void) {
    vector v_live = vector();
    slice sl_live = slice(NULL, 0);
    slice sl_child = slice(NULL, 0);
    slice sl_empty = slice(NULL, 0);
    int out = 0;
    int i, ok;

    v_live.reserve(32); /* avoid reallocation in the first pushes */
    for (i = 0; i < 8; i++)
        v_live.push_back(10 + i);
    sl_live = v_live.view();
    ck("av_sl_size", sl_live.size() == 8);
    ck("av_sl_live", sl_live.valid() == 1 && sl_live.stale() == 0);
    ok = 1;
    for (i = 0; i < 8; i++)
        if (sl_live.at_checked((size_t)i, &out) != 0 || out != 10 + i)
            ok = 0;
    ck("av_sl_read", ok);
    ck("av_sl_bounds", sl_live.at_checked(8, &out) == -1 &&
                          sl_live.at_checked(0, NULL) == -1);

    /* Sub-slice keeps the parent's generation and refuses out-of-window
     * indices even while live. */
    sl_child = sl_live.sub(2, 3);
    ck("av_sl_sub_live", sl_child.size() == 3 && sl_child.valid() == 1 &&
                             sl_child.stale() == 0);
    ck("av_sl_sub_read", sl_child.at_checked(0, &out) == 0 && out == 12 &&
                             sl_child.at_checked(2, &out) == 0 && out == 14 &&
                             sl_child.at_checked(3, &out) == -1);
    {
        slice sl_clamped = sl_live.sub(6, 99);
        slice sl_past = sl_live.sub(8, 1);
        ck("av_sl_sub_clamp", sl_clamped.size() == 2 && sl_past.size() == 0);
    }

    /* push_back that does NOT reallocate is documented not to invalidate. */
    v_live.push_back(99);
    ck("av_sl_push_noinval", sl_live.stale() == 0 && sl_live.valid() == 1 &&
                                sl_live.size() == 8);
    ck("av_sl_fixed_window",
       sl_live.at_checked(7, &out) == 0 && out == 17 &&
           v_live.at_checked(8, &out) == 0 && out == 99);

    /* Empty view: size 0, not valid, refuses reads. */
    sl_empty = v_live.view();
    v_live.clear();
    sl_empty = v_live.view();
    ck("av_sl_empty", sl_empty.size() == 0 && sl_empty.valid() == 0 &&
                          sl_empty.at_checked(0, &out) == -1);
    v_live.drop();
}

/* ---------------------------------------------- slice: reallocation refusal */
static void test_slice_realloc(void) {
    vector v_re = vector();
    slice sl_re = slice(NULL, 0);
    int out = 0;
    int i;

    v_re.reserve(8);
    for (i = 0; i < 8; i++)
        v_re.push_back(i);
    sl_re = v_re.view();
    ck("av_sl_re_setup", sl_re.valid() == 1 && sl_re.stale() == 0);

    ck("av_sl_re_grow_rc", v_re.reserve(64) == 0);
    ck("av_sl_re_stale", sl_re.stale() == 1 && sl_re.valid() == 0);
    ck("av_sl_re_snapshot", sl_re.size() == 8); /* fields unchanged */
    /* Repeated attempts after invalidation must all be refusals. */
    ck("av_sl_re_refuse1", sl_re.at_checked(0, &out) == -1);
    ck("av_sl_re_refuse2", sl_re.at_checked(0, &out) == -1);
    ck("av_sl_re_refuse3", sl_re.at_checked(3, &out) == -1);
    ck("av_sl_re_refuse4", sl_re.at_checked(7, &out) == -1);
    ck("av_sl_re_nullout", sl_re.at_checked(0, NULL) == -1);
    /* A fresh view of the same owner is live again. */
    {
        slice sl_fresh = v_re.view();
        ck("av_sl_re_fresh", sl_fresh.valid() == 1 && sl_fresh.stale() == 0 &&
                                 sl_fresh.at_checked(0, &out) == 0 && out == 0);
    }
    v_re.drop();
    /* drop() bumps generation again; the old slice keeps refusing. */
    ck("av_sl_re_after_drop", sl_re.stale() == 1 && sl_re.valid() == 0 &&
                                  sl_re.at_checked(0, &out) == -1);
}

/* ------------------------------------- slice: every shortening mutator */
static void test_slice_mutators(void) {
    int out = 0;

    /* erase */
    {
        vector v_e = vector();
        slice s_e = slice(NULL, 0);
        v_e.push_back(1); v_e.push_back(2); v_e.push_back(3);
        s_e = v_e.view();
        v_e.erase(0, NULL);
        ck("av_sl_erase", s_e.stale() == 1 && s_e.valid() == 0 &&
                              s_e.at_checked(0, &out) == -1 &&
                              s_e.at_checked(1, &out) == -1);
        v_e.drop();
    }
    /* pop_back */
    {
        vector v_p = vector();
        slice s_p = slice(NULL, 0);
        v_p.push_back(1); v_p.push_back(2);
        s_p = v_p.view();
        v_p.pop_back(&out);
        ck("av_sl_pop", s_p.stale() == 1 && s_p.valid() == 0 &&
                           s_p.at_checked(0, &out) == -1);
        v_p.drop();
    }
    /* clear */
    {
        vector v_c = vector();
        slice s_c = slice(NULL, 0);
        v_c.push_back(1); v_c.push_back(2);
        s_c = v_c.view();
        v_c.clear();
        ck("av_sl_clear", s_c.stale() == 1 && s_c.valid() == 0 &&
                              s_c.at_checked(0, &out) == -1);
        v_c.drop();
    }
    /* insert (even without a realloc, generation is bumped) */
    {
        vector v_i = vector();
        slice s_i = slice(NULL, 0);
        v_i.reserve(16);
        v_i.push_back(1); v_i.push_back(2);
        s_i = v_i.view();
        v_i.insert(0, 9);
        ck("av_sl_insert", s_i.stale() == 1 && s_i.valid() == 0 &&
                               s_i.at_checked(0, &out) == -1);
        v_i.drop();
    }
    /* shrink_to_fit */
    {
        vector v_s = vector();
        slice s_s = slice(NULL, 0);
        v_s.reserve(32);
        v_s.push_back(7);
        s_s = v_s.view();
        v_s.shrink_to_fit();
        ck("av_sl_shrink", s_s.stale() == 1 && s_s.valid() == 0 &&
                               s_s.at_checked(0, &out) == -1);
        v_s.drop();
    }
    /* copy_from */
    {
        vector v_d = vector();
        vector v_src = vector();
        slice s_d = slice(NULL, 0);
        v_d.push_back(1);
        v_src.push_back(5); v_src.push_back(6);
        s_d = v_d.view();
        v_d.copy_from(&v_src);
        ck("av_sl_copy", s_d.stale() == 1 && s_d.valid() == 0 &&
                             s_d.at_checked(0, &out) == -1);
        v_d.drop();
        v_src.drop();
    }
    /* sub-slice of a stale parent: empty, non-stale, refuses; a live child
     * inherits staleness when the owner later mutates. */
    {
        vector v_sub = vector();
        slice s_parent = slice(NULL, 0);
        slice s_child = slice(NULL, 0);
        slice s_again = slice(NULL, 0);
        v_sub.push_back(1); v_sub.push_back(2); v_sub.push_back(3);
        s_parent = v_sub.view();
        s_child = s_parent.sub(1, 2);
        ck("av_sl_inherit_live", s_child.valid() == 1 && s_child.stale() == 0 &&
                                     s_child.at_checked(0, &out) == 0 && out == 2);
        v_sub.erase(0, NULL);
        ck("av_sl_inherit_stale", s_parent.stale() == 1 && s_child.stale() == 1 &&
                                      s_child.valid() == 0 &&
                                      s_child.at_checked(0, &out) == -1);
        s_again = s_parent.sub(0, 1);
        ck("av_sl_stale_sub", s_again.size() == 0 && s_again.valid() == 0 &&
                                  s_again.stale() == 0 &&
                                  s_again.at_checked(0, &out) == -1);
        ck("av_sl_stale_sub_repeat", s_again.at_checked(0, &out) == -1);
        v_sub.drop();
    }
}

/* -------------------------------------------------- string_view: live window */
static void test_view_live(void) {
    string s_live = string("hello");
    string_view sv_live = string_view(NULL, 0);
    string_view sv_direct = string_view("abcXYZ", 3);
    const char *p_live;

    /* Reserve BEFORE taking the view so the subsequent append is in-place;
     * calling equals()/is_valid_utf8() on a view is only ever done while its
     * buffer is live. */
    ck("av_vw_reserve_rc", s_live.reserve(64) == 0);
    sv_live = s_live.to_view();
    p_live = sv_live.data();

    ck("av_vw_size", sv_live.size() == 5);
    ck("av_vw_identity", p_live == s_live.c_str() && p_live != (const char *)0);
    ck("av_vw_content", sv_live.equals("hello") == 1 &&
                           sv_live.equals("hell") == 0 &&
                           sv_live.equals("hello!") == 0);
    ck("av_vw_utf8", sv_live.is_valid_utf8() == 1);
    ck("av_vw_direct", sv_direct.size() == 3 && sv_direct.equals("abc") == 1 &&
                           sv_direct.equals("abcXYZ") == 0);

    /* Metadata gate permits a live view whose generation is current. */
    ck("av_vw_permit_live",
       view_permit(p_live, sv_live.size(), stdlib_alloc_generation(p_live)) == 1);

    /* Fixed-window semantics: in-place append keeps the pointer and the
     * recorded size; only the owner grows. */
    ck("av_vw_append_rc", s_live.append("-more") == 0);
    ck("av_vw_fixed", sv_live.data() == p_live && sv_live.size() == 5 &&
                          s_live.size() == 10);
    ck("av_vw_window_not_grown", sv_live.equals("hello") == 1 &&
                                     sv_live.equals("hello-more") == 0);
    s_live.drop();
}

/* -------------------------- string_view: reallocation -> metadata refusal */
static void test_view_realloc(void) {
    string s_re = string("short");
    string_view sv_re = s_re.to_view();
    const char *p_re = sv_re.data();
    size_t n_re = sv_re.size();
    unsigned long long g_re = stdlib_alloc_generation(p_re);
    const char *after_re;

    ck("av_vw_re_setup", n_re == 5 && g_re != 0ULL &&
                            view_permit(p_re, n_re, g_re) == 1);
    ck("av_vw_re_append_rc", s_re.append("-a-much-longer-suffix") == 0);
    after_re = s_re.c_str();

    ck("av_vw_re_moved", after_re != p_re);
    ck("av_vw_re_snapshot", sv_re.data() == p_re && sv_re.size() == n_re);
    ck("av_vw_re_gen_stale", stdlib_alloc_check_generation(p_re, g_re) == 0);
    ck("av_vw_re_new_gen", stdlib_alloc_generation(after_re) != 0ULL &&
                              stdlib_alloc_generation(after_re) != g_re);
    /* Refusal is repeatable and metadata-only; no stale dereference. */
    ck("av_vw_re_refuse1", view_permit(p_re, n_re, g_re) == 0);
    ck("av_vw_re_refuse2", view_permit(p_re, n_re, g_re) == 0);
    ck("av_vw_re_refuse3", view_permit(p_re, n_re, g_re) == 0);
    /* A fresh view/generation pair of the new buffer is permitted. */
    ck("av_vw_re_fresh", view_permit(after_re, s_re.size(),
                                     stdlib_alloc_generation(after_re)) == 1);
    s_re.drop();
}

/* --------------------------------- string_view: reserve and drop refusal */
static void test_view_reserve_drop(void) {
    string s_rd = string("abcdef");
    string_view sv_rd = s_rd.to_view();
    const char *p_rd = sv_rd.data();
    size_t n_rd = sv_rd.size();
    unsigned long long g_rd = stdlib_alloc_generation(p_rd);

    ck("av_vw_rd_setup", n_rd == 6 && g_rd != 0ULL);
    ck("av_vw_rd_reserve_rc", s_rd.reserve(64) == 0);
    ck("av_vw_rd_reserve_refuse", s_rd.c_str() != p_rd &&
                                     sv_rd.data() == p_rd &&
                                     sv_rd.size() == n_rd &&
                                     view_permit(p_rd, n_rd, g_rd) == 0);

    /* drop(): old buffer is no longer a live allocation. */
    s_rd.drop();
    ck("av_vw_rd_owner_empty", s_rd.valid() == 0 && s_rd.c_str()[0] == '\0');
    ck("av_vw_rd_fields", sv_rd.data() == p_rd && sv_rd.size() == n_rd);
    ck("av_vw_rd_gen_stale", stdlib_alloc_check_generation(p_rd, g_rd) == 0);
    ck("av_vw_rd_refuse1", view_permit(p_rd, n_rd, g_rd) == 0);
    ck("av_vw_rd_refuse2", view_permit(p_rd, n_rd, g_rd) == 0);
    ck("av_vw_rd_size_zero_refuse", view_permit(p_rd, 0, g_rd) == 0);
    ck("av_vw_rd_gen_zero_refuse", view_permit(p_rd, n_rd, 0ULL) == 0);
    ck("av_vw_rd_null_refuse", view_permit((const char *)0, n_rd, g_rd) == 0);
}

/* ------------------------- string_view: degenerate views, no stale deref */
static void test_view_edges(void) {
    string s_edge = string("abc");
    string_view sv_zero = string_view(NULL, 0);
    string_view sv_len = string_view(NULL, 3);
    string_view sv_bad = string_view("\xc3\x28", 2);
    string_view sv_nul = string_view("a\0b", 3);
    unsigned long long g_edge = stdlib_alloc_generation(s_edge.c_str());

    ck("av_vw_zero", sv_zero.size() == 0 && sv_zero.data() == (const char *)0 &&
                        sv_zero.equals("") == 1 && sv_zero.equals("x") == 0 &&
                        sv_zero.equals(NULL) == 0 && sv_zero.is_valid_utf8() == 1);
    ck("av_vw_zero_refuse", view_permit(sv_zero.data(), sv_zero.size(), g_edge) == 0);
    ck("av_vw_null_len", sv_len.size() == 3 && sv_len.equals("abc") == 0 &&
                            sv_len.equals(NULL) == 0 &&
                            sv_len.is_valid_utf8() == 0);
    ck("av_vw_null_len_refuse", view_permit(sv_len.data(), sv_len.size(), g_edge) == 0);
    ck("av_vw_bad_utf8", sv_bad.size() == 2 && sv_bad.is_valid_utf8() == 0);
    ck("av_vw_embedded_nul", sv_nul.size() == 3 && sv_nul.is_valid_utf8() == 1 &&
                                sv_nul.equals("a\0b") == 0);
    s_edge.drop();
}

/* ------------------- repeated attempts after invalidation, cross-check */
static void test_repeated_refusal(void) {
    vector v_rep = vector();
    slice s_rep = slice(NULL, 0);
    string t_rep = string("xyz");
    string_view tv_rep = t_rep.to_view();
    const char *p_rep = tv_rep.data();
    size_t n_rep = tv_rep.size();
    unsigned long long g_rep = stdlib_alloc_generation(p_rep);
    int out = 0;
    int i, slice_ok, view_ok;

    v_rep.push_back(1); v_rep.push_back(2);
    s_rep = v_rep.view();
    t_rep.reserve(128); /* invalidate the string_view */
    v_rep.erase(0, NULL); /* invalidate the slice */
    slice_ok = 1;
    view_ok = 1;
    for (i = 0; i < 25; i++) {
        if (s_rep.at_checked(0, &out) != -1)
            slice_ok = 0;
        if (view_permit(p_rep, n_rep, g_rep) != 0)
            view_ok = 0;
    }
    ck("av_rep_slice_25", slice_ok == 1 && s_rep.stale() == 1 &&
                              s_rep.valid() == 0);
    ck("av_rep_view_25", view_ok == 1 && tv_rep.data() == p_rep &&
                             tv_rep.size() == n_rep &&
                             stdlib_alloc_check_generation(p_rep, g_rep) == 0);
    v_rep.drop();
    t_rep.drop();
}

int main(void) {
    test_slice_live();
    test_slice_realloc();
    test_slice_mutators();
    test_view_live();
    test_view_realloc();
    test_view_reserve_drop();
    test_view_edges();
    test_repeated_refusal();

    print("ADV_STALE_VIEWS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
