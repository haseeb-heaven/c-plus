/* Container-safety regression suite -- pointer/slice invalidation observed
 * ONLY through generation metadata.
 *
 * A slice returned by vector::view() records the owner's generation counter.
 * Mutating operations that can move or shorten storage bump that counter; the
 * slice then reports stale() == 1, valid() == 0 and refuses reads.  Likewise,
 * checked-allocator blocks carry a generation token that is retired by
 * stdlib_realloc / stdlib_transfer_ownership and forgotten by stdlib_release.
 *
 * INVARIANT: this test NEVER dereferences a stale pointer.  After an
 * invalidating operation it only calls metadata observers (stale, valid,
 * size, at_checked on the slice, stdlib_alloc_check_generation, etc.).
 * at_checked is the library's checked accessor; it refuses stale slices before
 * reading memory.  No raw pointer captured before a reallocation is read.
 *
 * SELF-CHECKING: "<tag> <1|0>" per check (1 = pass); final line
 * "CONTAINER_SAFETY <checks> <failures>".  All loops are bounded (none here).
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"

static int g_checks = 0;
static int g_failed = 0;

static void check(const char *tag, int cond) {
    g_checks++;
    if (!cond)
        g_failed++;
    print(tag, cond ? 1 : 0);
}

/* ---- slice lifetime across a non-reallocating push and a real realloc ---- */
static void g_vector_slices(void) {
    int o = 0;
    vector gv_s = vector();
    gv_s.reserve(16);
    gv_s.push_back(10);
    gv_s.push_back(20);
    gv_s.push_back(30);

    {
        slice gv_first = gv_s.view();
        check("g_slice_valid", gv_first.valid() == 1 && gv_first.stale() == 0 &&
                               gv_first.size() == 3 &&
                               gv_first.at_checked(1, &o) == 0 && o == 20);
        {
            slice gv_sub = gv_first.sub(1, 2);
            check("g_slice_sub", gv_sub.valid() == 1 && gv_sub.size() == 2 &&
                                 gv_sub.at_checked(0, &o) == 0 && o == 20 &&
                                 gv_sub.at_checked(1, &o) == 0 && o == 30);
        }
        {
            slice gv_oob = gv_first.sub(9, 1);
            check("g_sub_oob", gv_oob.size() == 0 && gv_oob.valid() == 0);
        }

        /* push_back with spare capacity does not move storage -> still valid */
        gv_s.push_back(40);
        check("g_slice_no_realloc", gv_first.stale() == 0 && gv_first.valid() == 1 &&
                                    gv_first.size() == 3 && gv_s.size() == 4);

        /* reserve() reallocates -> metadata must mark every old slice stale */
        gv_s.reserve(64);
        check("g_slice_realloc", gv_first.stale() == 1 && gv_first.valid() == 0 &&
                                 gv_first.at_checked(0, &o) == -1);
        {
            slice gv_stalesub = gv_first.sub(0, 1);
            check("g_slice_stale_sub", gv_stalesub.size() == 0 &&
                                       gv_stalesub.valid() == 0);
        }
        {
            slice gv_fresh = gv_s.view();
            check("g_slice_fresh", gv_fresh.valid() == 1 && gv_fresh.size() == 4 &&
                                   gv_fresh.at_checked(3, &o) == 0 && o == 40);
            gv_s.drop();
            check("g_slice_drop_stale", gv_first.stale() == 1 &&
                                        gv_fresh.stale() == 1 &&
                                        gv_fresh.valid() == 0);
        }
        check("g_slice_after_drop", gv_first.stale() == 1);
    }
}

/* ---- each shape-changing operation bumps the generation ------------------ */
static void g_vector_mutations(void) {
    int o = 0;
    vector gv_m = vector();
    gv_m.reserve(16);
    gv_m.push_back(1);
    gv_m.push_back(2);
    gv_m.push_back(3);

    {
        slice gm_pop = gv_m.view();
        gv_m.pop_back(&o);
        check("g_pop_stale", gm_pop.stale() == 1 && gm_pop.valid() == 0 && o == 3);
    }
    {
        slice gm_ins = gv_m.view();
        gv_m.insert(0, 9);
        check("g_insert_stale", gm_ins.stale() == 1 && gm_ins.valid() == 0 &&
                                gv_m.at_checked(0, &o) == 0 && o == 9);
    }
    {
        slice gm_era = gv_m.view();
        gv_m.erase(0, &o);
        check("g_erase_stale", gm_era.stale() == 1 && gm_era.valid() == 0 && o == 9);
    }
    {
        slice gm_clr = gv_m.view();
        gv_m.clear();
        check("g_clear_stale", gm_clr.stale() == 1 && gm_clr.valid() == 0 &&
                               gv_m.size() == 0);
    }
    {
        slice gm_empty = gv_m.view();
        check("g_empty_view", gm_empty.stale() == 0 && gm_empty.valid() == 0 &&
                              gm_empty.size() == 0 &&
                              gm_empty.at_checked(0, &o) == -1);
    }
    gv_m.drop();
}

static void g_vector_shrink_copy(void) {
    int o = 0;
    vector gv_sc = vector();
    vector gv_sc_src = vector();
    gv_sc.reserve(32);
    gv_sc.push_back(5);
    gv_sc_src.push_back(7);
    {
        slice gsc_shrink = gv_sc.view();
        gv_sc.shrink_to_fit();
        check("g_shrink_stale", gsc_shrink.stale() == 1 && gsc_shrink.valid() == 0 &&
                                gv_sc.capacity() == 1);
    }
    {
        slice gsc_copy = gv_sc.view();
        gv_sc.copy_from(&gv_sc_src);
        check("g_copy_stale", gsc_copy.stale() == 1 && gsc_copy.valid() == 0 &&
                              gv_sc.at_checked(0, &o) == 0 && o == 7);
    }
    gv_sc.drop();
    gv_sc_src.drop();
}

/* ---- allocator generation tokens; no stale pointer is ever read ---------- */
static void g_alloc_meta(void) {
    void *ga_p1 = stdlib_alloc(64);
    void *ga_p2 = NULL;
    void *ga_dst = NULL;
    void *ga_self = NULL;
    unsigned long long ga_gen = 0;

    check("g_alloc_size", ga_p1 != NULL && stdlib_alloc_size(ga_p1) == 64 &&
                          stdlib_alloc_length(ga_p1) == 64);
    ga_gen = stdlib_alloc_generation(ga_p1);
    check("g_gen_token", ga_gen > 0 &&
                         stdlib_alloc_check_generation(ga_p1, ga_gen) == 1 &&
                         stdlib_alloc_check_generation(ga_p1, ga_gen + 1) == 0);

    /* realloc retires the OLD identity (in-place or moved) and assigns a new
     * generation.  We only inspect metadata of the old pointer afterwards. */
    ga_p2 = stdlib_realloc(ga_p1, 128);
    check("g_realloc_retires", ga_p2 != NULL &&
                               stdlib_alloc_check_generation(ga_p1, ga_gen) == 0 &&
                               stdlib_alloc_generation(ga_p2) != ga_gen &&
                               stdlib_alloc_size(ga_p2) == 128);
    stdlib_release(ga_p2);
    check("g_freed_meta", stdlib_alloc_generation(ga_p2) == 0 &&
                          stdlib_alloc_check_generation(ga_p2, ga_gen) == 0 &&
                          stdlib_alloc_size(ga_p2) == 0);

    /* ownership transfer retires the source token even though the address is
     * unchanged; metadata is the only invalidation signal used here. */
    ga_p1 = stdlib_alloc(32);
    ga_gen = stdlib_alloc_generation(ga_p1);
    check("g_transfer", stdlib_transfer_ownership(&ga_dst, &ga_p1) == 0 &&
                        ga_p1 == NULL && ga_dst != NULL &&
                        stdlib_alloc_check_generation(ga_dst, ga_gen) == 0 &&
                        stdlib_alloc_generation(ga_dst) > ga_gen);
    stdlib_release(ga_dst);

    ga_self = stdlib_alloc(8);
    check("g_transfer_self", stdlib_transfer_ownership(&ga_self, &ga_self) == -1);
    stdlib_release(ga_self);
    check("g_meta_terminated", 1);
}

int main(void) {
    g_vector_slices();
    g_vector_mutations();
    g_vector_shrink_copy();
    g_alloc_meta();
    check("g_terminated", 1);
    print("CONTAINER_SAFETY", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
