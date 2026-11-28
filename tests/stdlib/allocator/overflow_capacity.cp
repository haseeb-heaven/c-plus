/* Phase 5: capacity-doubling and power-of-two rounding arithmetic.
 *
 * Covered paths (all guards are BEFORE the allocator call):
 *   stdlib_map_next_pow2(want) ........ cap <<= 1 guarded by SIZE_MAX >> 2
 *   stdlib_map_rebuild_capacity() ..... capacity * 2 guarded by SIZE_MAX >> 2
 *   vector::push_back / insert ........ space * 2 guarded by
 *                                        (LIMIT/sizeof(int))/2
 *   deque::stdlib_make_room ........... same guard, wrapped ring copy
 *   stack::push / queue::push ......... same guard
 *   intset/intmap/strmap::grow ........ next_pow2 -> rebuild(cap) size guard
 *   stdlib_ring_copy(newspace) ........ newspace > LIMIT/sizeof(int) guard
 *   container rebuild(cap) ............ cap > LIMIT/sizeof(element) guard
 *
 * STDLIB_ALLOC_LIMIT is 4096 in source.  A 1024-int buffer is exactly 4096
 * bytes; the next doubling (2048) must be refused with seq delta 0.
 * Self-checking: "<tag> <0|1>", final "OVERFLOW_CAPACITY <n> <f>".
 */
#ifndef STDLIB_ALLOC_LIMIT
#define STDLIB_ALLOC_LIMIT ((size_t)4096)
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"

#define OVC_LIMIT ((size_t)4096)

static int ovc_checks = 0;
static int ovc_failed = 0;

static void ovc_chk(const char *tag, int cond) {
    ovc_checks++;
    if (!cond)
        ovc_failed++;
    print(tag, cond ? 1 : 0);
}

static int ovc_cb_calls;
static int ovc_cb_cat;
static cplus_exit_code ovc_cb_code;
static char ovc_cb_msg[128];

static void ovc_cb(int cat, cplus_exit_code code, const char *msg) {
    ovc_cb_calls++;
    ovc_cb_cat = cat;
    ovc_cb_code = code;
    if (msg != NULL) {
        size_t n = strlen(msg);
        if (n >= sizeof(ovc_cb_msg))
            n = sizeof(ovc_cb_msg) - 1;
        memcpy(ovc_cb_msg, msg, n);
        ovc_cb_msg[n] = '\0';
    }
}

static void ovc_cb_reset(void) {
    ovc_cb_calls = 0;
    ovc_cb_cat = -1;
    ovc_cb_code = CPLUS_EXIT_OK;
    ovc_cb_msg[0] = '\0';
}

/* ---- pure arithmetic helpers ------------------------------------------ */
static void ovc_helpers(void) {
    ovc_chk("ovc_pow2_sxmax", stdlib_map_next_pow2(SIZE_MAX) == 0);
    ovc_chk("ovc_pow2_u64max", stdlib_map_next_pow2((size_t)UINT64_MAX) == 0);
    /* next_pow2 stops at 2^62 (SIZE_MAX>>2 + 1); rebuild() still refuses it. */
    ovc_chk("ovc_pow2_huge",
            stdlib_map_next_pow2((size_t)1 << 62) == ((size_t)1 << 62));
    ovc_chk("ovc_pow2_ok", stdlib_map_next_pow2((size_t)1000) == 1024 &&
                               stdlib_map_next_pow2((size_t)8) == 8 &&
                               stdlib_map_next_pow2((size_t)1) == 8);
    ovc_chk("ovc_rebuild_cap_overflow",
            stdlib_map_rebuild_capacity(SIZE_MAX - 1u, SIZE_MAX - 1u) == 0);
    ovc_chk("ovc_rebuild_cap_double",
            stdlib_map_rebuild_capacity((size_t)700, (size_t)1024) == 2048);
    ovc_chk("ovc_rebuild_cap_same",
            stdlib_map_rebuild_capacity((size_t)10, (size_t)1024) == 1024);
    ovc_chk("ovc_rebuild_cap_zero", stdlib_map_rebuild_capacity(0, 0) == 8);
}

/* ---- vector: doubling at the 512 -> 1024 -> refused boundary --------- */
static void ovc_vector(void) {
    vector ovc_v = vector();
    long seq_before;
    stdlib_telemetry_stats pre, post;
    int i, ok;

    ok = ovc_v.reserve(512) == 0 && ovc_v.capacity() == 512;
    ovc_chk("ovc_vec_reserve512", ok);
    for (i = 0; i < 512; i++)
        ok = ok && ovc_v.push_back(i) == 0;
    ovc_chk("ovc_vec_fill512", ok && ovc_v.size() == 512 &&
                                   ovc_v.capacity() == 512);
    ok = ovc_v.push_back(1000) == 0;
    ovc_chk("ovc_vec_double1024", ok && ovc_v.capacity() == 1024 &&
                                      ovc_v.size() == 513);
    for (i = 513; i < 1024; i++)
        ok = ok && ovc_v.push_back(i) == 0;
    ovc_chk("ovc_vec_fill1024", ok && ovc_v.size() == 1024 &&
                                    ovc_v.capacity() == 1024);

    ovc_cb_reset();
    stdlib_telemetry_snapshot(&pre);
    seq_before = stdlib_alloc_seq();
    ok = ovc_v.push_back(9999) == -1 && ovc_v.insert(0, 9999) == -1;
    stdlib_telemetry_snapshot(&post);
    ovc_chk("ovc_vec_doubling_refused", ok);
    /* push_back reports the guarded overflow; insert returns -1 silently. */
    ovc_chk("ovc_vec_no_alloc",
            stdlib_alloc_seq() == seq_before &&
                post.alloc_count == pre.alloc_count &&
                post.api_failures == pre.api_failures + 1 &&
                ovc_cb_calls == 1 && ovc_cb_cat == STDLIB_CATEGORY_RANGE &&
                ovc_cb_code == CPLUS_EXIT_BOUNDS &&
                strcmp(ovc_cb_msg, "vector::push_back: capacity overflow") == 0);
    {
        int first = -1, last = -1;
        ok = ovc_v.size() == 1024 && ovc_v.capacity() == 1024 &&
             ovc_v.at_checked(0, &first) == 0 && first == 0 &&
             ovc_v.at_checked(1023, &last) == 0 && last == 1023;
    }
    ovc_chk("ovc_vec_state_unchanged", ok);
    ovc_v.drop();
}

/* ---- deque: same guard with a wrapped ring ---------------------------- */
static void ovc_deque(void) {
    deque ovc_dq = deque();
    long seq_before;
    int i, ok = 1, val = 0;

    ok = ovc_dq.reserve(512) == 0;
    for (i = 0; i < 512; i++)
        ok = ok && ovc_dq.push_back(i) == 0;
    ok = ok && ovc_dq.push_front(7000) == 0 && ovc_dq.capacity() == 1024;
    ovc_chk("ovc_deque_grow", ok && ovc_dq.size() == 513 &&
                                  ovc_dq.capacity() == 1024);
    /* Wrap the ring: pop several from the front, append to refill.  The
     * first pop is the pushed front element, the rest are the old backs. */
    val = 0;
    ok = ovc_dq.pop_front(&val) == 0 && val == 7000;
    for (i = 1; i < 64; i++)
        ok = ok && ovc_dq.pop_front(&val) == 0 && val == i - 1;
    ovc_chk("ovc_deque_wrap_pops", ok);
    for (i = 0; i < 575; i++)
        ok = ok && ovc_dq.push_back(8000 + i) == 0;
    ovc_chk("ovc_deque_wrapped_full",
            ok && ovc_dq.size() == 1024 && ovc_dq.capacity() == 1024);
    seq_before = stdlib_alloc_seq();
    {
        int r1 = ovc_dq.push_back(9999);
        int r2 = ovc_dq.push_front(9999);
        ovc_chk("ovc_deque_doubling_refused", r1 == -1 && r2 == -1);
        ovc_chk("ovc_deque_no_alloc", stdlib_alloc_seq() == seq_before);
        ovc_chk("ovc_deque_state", ovc_dq.size() == 1024 &&
                                       ovc_dq.capacity() == 1024);
    }
    ovc_dq.drop();
}

/* ---- stack / queue: same doubling guard ------------------------------- */
static void ovc_stack_queue(void) {
    stack ovc_st = stack();
    queue ovc_q = queue();
    long seq_before;
    int i, ok = 1, val = 0;

    for (i = 0; i < 1024; i++)
        ok = ok && ovc_st.push(i) == 0;
    ovc_chk("ovc_stack_fill", ok && ovc_st.size() == 1024);
    seq_before = stdlib_alloc_seq();
    ok = ovc_st.push(9999) == -1 && stdlib_alloc_seq() == seq_before &&
         ovc_st.size() == 1024 && ovc_st.top_checked(&val) == 0 && val == 1023;
    ovc_chk("ovc_stack_doubling_refused", ok);

    for (i = 0; i < 1024; i++)
        ok = ok && ovc_q.push(i) == 0;
    ovc_chk("ovc_queue_fill", ok && ovc_q.size() == 1024);
    seq_before = stdlib_alloc_seq();
    ok = ovc_q.push(9999) == -1 && stdlib_alloc_seq() == seq_before &&
         ovc_q.size() == 1024 && ovc_q.front_checked(&val) == 0 && val == 0;
    ovc_chk("ovc_queue_doubling_refused", ok);

    ovc_st.drop();
    ovc_q.drop();
}

/* ---- ring_copy guard and map rebuild cap guard ------------------------ */
static void ovc_ring_and_maps(void) {
    deque ovc_dq2 = deque();
    intset ovc_is = intset();
    intmap ovc_im = intmap();
    strmap ovc_sm = strmap();
    long seq_before;
    int ok;

    ovc_cb_reset();
    seq_before = stdlib_alloc_seq();
    ok = ovc_dq2.reserve(SIZE_MAX) == -1 && stdlib_alloc_seq() == seq_before &&
         ovc_cb_calls == 1 && ovc_cb_cat == STDLIB_CATEGORY_RANGE &&
         ovc_cb_code == CPLUS_EXIT_BOUNDS &&
         strcmp(ovc_cb_msg, "ring buffer: capacity too large") == 0 &&
         ovc_dq2.size() == 0 && ovc_dq2.capacity() == 0;
    ovc_chk("ovc_ring_copy_guard", ok);

    seq_before = stdlib_alloc_seq();
    ok = ovc_is.grow(SIZE_MAX) == -1 && ovc_im.grow(SIZE_MAX) == -1 &&
         ovc_sm.grow(SIZE_MAX) == -1 && stdlib_alloc_seq() == seq_before &&
         ovc_is.valid() == 0 && ovc_im.valid() == 0 && ovc_sm.valid() == 0;
    ovc_chk("ovc_map_grow_sxmax", ok);

    /* fill intset until the 1024 -> 2048 rebuild is refused */
    {
        int i, rc, failures = 0;
        size_t last_size;
        for (i = 0; i < 900; i++) {
            rc = ovc_is.insert(i);
            if (rc != 0)
                failures++;
        }
        last_size = ovc_is.size();
        ovc_cb_reset();
        seq_before = stdlib_alloc_seq();
        rc = ovc_is.insert(9999);
        ovc_chk("ovc_intset_rebuild_refused",
                rc == -1 && failures > 0 && stdlib_alloc_seq() == seq_before &&
                    ovc_is.size() == last_size && ovc_is.valid() == 1);
    }
    ovc_dq2.drop();
    ovc_is.drop();
    ovc_im.drop();
    ovc_sm.drop();
}

int main(void) {
    stdlib_set_error_callback(ovc_cb);
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();
    ovc_helpers();
    ovc_vector();
    ovc_deque();
    ovc_stack_queue();
    ovc_ring_and_maps();
    stdlib_set_error_callback(NULL);
    print("OVERFLOW_CAPACITY", ovc_checks);
    print("OVERFLOW_CAPACITY_FAILURES", ovc_failed);
    return ovc_failed == 0 ? 0 : 1;
}
