/* Phase 5: reallocation-size arithmetic and failure-atomic resize.
 *
 * Covered paths:
 *   stdlib_realloc(ptr, bytes) ........ actual = bytes==0?1:bytes, then
 *                                       bytes > LIMIT guard BEFORE any
 *                                       cplus_realloc() / stdlib_alloc() call
 *   stdlib_realloc(NULL, bytes) ....... forwards to stdlib_alloc's limit guard
 *   stdlib_realloc(untracked, bytes) .. metadata lookup refusal, no allocator
 *   vector/string growth .............. new buffer size computed from the
 *                                       guarded capacity, old released only
 *                                       after the new allocation succeeds
 *
 * Categories/codes are documented per check: over-limit realloc is RANGE /
 * CPLUS_EXIT_BOUNDS, over-limit alloc-forward is MEMORY / CPLUS_EXIT_RESOURCE,
 * an untracked pointer is INVALID / CPLUS_EXIT_UNSUPPORTED.
 * STDLIB_ALLOC_LIMIT = 4096 in source.  Self-checking: "<tag> <0|1>",
 * final "OVERFLOW_REALLOC <n> <f>".
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
#include "stdlib/string.hp"

#define OVR_LIMIT ((size_t)4096)

static int ovr_checks = 0;
static int ovr_failed = 0;

static void ovr_chk(const char *tag, int cond) {
    ovr_checks++;
    if (!cond)
        ovr_failed++;
    print(tag, cond ? 1 : 0);
}

static int ovr_cb_calls;
static int ovr_cb_cat;
static cplus_exit_code ovr_cb_code;
static char ovr_cb_msg[128];

static void ovr_cb(int cat, cplus_exit_code code, const char *msg) {
    ovr_cb_calls++;
    ovr_cb_cat = cat;
    ovr_cb_code = code;
    if (msg != NULL) {
        size_t n = strlen(msg);
        if (n >= sizeof(ovr_cb_msg))
            n = sizeof(ovr_cb_msg) - 1;
        memcpy(ovr_cb_msg, msg, n);
        ovr_cb_msg[n] = '\0';
    }
}

static void ovr_cb_reset(void) {
    ovr_cb_calls = 0;
    ovr_cb_cat = -1;
    ovr_cb_code = CPLUS_EXIT_OK;
    ovr_cb_msg[0] = '\0';
}

static void ovr_realloc_sizes(void) {
    void *block;
    long seq_before;
    stdlib_telemetry_stats pre, post;
    int ok;

    block = stdlib_alloc(64);
    ovr_chk("ovr_block", block != NULL && stdlib_alloc_size(block) == 64);
    if (block == NULL)
        return;
    memset(block, 0x37, 64);

    ovr_cb_reset();
    stdlib_telemetry_snapshot(&pre);
    seq_before = stdlib_alloc_seq();
    ok = stdlib_realloc(block, SIZE_MAX) == NULL &&
         stdlib_realloc(block, OVR_LIMIT + 1) == NULL &&
         stdlib_realloc(block, (size_t)UINT64_MAX) == NULL;
    stdlib_telemetry_snapshot(&post);
    ovr_chk("ovr_size_refused", ok);
    ovr_chk("ovr_size_no_alloc",
            stdlib_alloc_seq() == seq_before &&
                post.alloc_count == pre.alloc_count &&
                post.container_growths == pre.container_growths &&
                post.api_failures == pre.api_failures + 3 &&
                ovr_cb_calls == 3 && ovr_cb_cat == STDLIB_CATEGORY_RANGE &&
                ovr_cb_code == CPLUS_EXIT_BOUNDS &&
                strcmp(ovr_cb_msg,
                       "stdlib_realloc: request exceeds STDLIB_ALLOC_LIMIT") == 0);
    ovr_chk("ovr_size_state_unchanged",
            stdlib_alloc_size(block) == 64 &&
                ((unsigned char *)block)[0] == 0x37 &&
                ((unsigned char *)block)[63] == 0x37);

    /* NULL + over-limit is refused by stdlib_realloc's own guard BEFORE it
     * can forward to stdlib_alloc: RANGE / CPLUS_EXIT_BOUNDS, seq delta 0. */
    ovr_cb_reset();
    seq_before = stdlib_alloc_seq();
    ok = stdlib_realloc(NULL, SIZE_MAX) == NULL &&
         stdlib_alloc_seq() == seq_before && ovr_cb_calls == 1 &&
         ovr_cb_cat == STDLIB_CATEGORY_RANGE &&
         ovr_cb_code == CPLUS_EXIT_BOUNDS &&
         strcmp(ovr_cb_msg,
                "stdlib_realloc: request exceeds STDLIB_ALLOC_LIMIT") == 0;
    ovr_chk("ovr_null_forward", ok);

    /* A pointer the checked allocator does not own is refused after a
     * metadata lookup, without touching the allocator or the pointer. */
    {
        int untracked = 12345;
        ovr_cb_reset();
        seq_before = stdlib_alloc_seq();
        ok = stdlib_realloc(&untracked, 8) == NULL &&
             stdlib_alloc_seq() == seq_before && untracked == 12345 &&
             ovr_cb_calls == 1 && ovr_cb_cat == STDLIB_CATEGORY_INVALID &&
             ovr_cb_code == CPLUS_EXIT_UNSUPPORTED &&
             strcmp(ovr_cb_msg,
                    "stdlib_realloc: pointer is not a live stdlib block") == 0;
        ovr_chk("ovr_untracked", ok);
    }

    /* Realloc to zero maps to one byte, like stdlib_alloc(0). */
    {
        void *small = stdlib_realloc(block, 0);
        ovr_chk("ovr_zero_bytes", small != NULL && stdlib_alloc_size(small) == 1);
        if (small != NULL)
            block = small;
    }
    {
        void *grown = stdlib_realloc(block, 128);
        ovr_chk("ovr_grow_bytes",
                grown != NULL && stdlib_alloc_size(grown) == 128 &&
                    ((unsigned char *)grown)[0] == 0x37);
        if (grown != NULL)
            block = grown;
    }
    stdlib_release(block);
}

/* Vector/string growth reallocates a correctly sized new buffer and is
 * failure-atomic: an injected failure leaves the old buffer and metadata. */
static void ovr_growth(void) {
    vector ovr_v = vector();
    string ovr_s = string("abc");
    long seq_before;
    stdlib_telemetry_stats pre, post;
    int i, ok;

    ok = ovr_v.reserve(8) == 0;
    for (i = 0; i < 8; i++)
        ok = ok && ovr_v.push_back(i) == 0;
    ovr_chk("ovr_vec_prepare", ok && ovr_v.capacity() == 8);

    seq_before = stdlib_alloc_seq();
    ok = ovr_v.reserve(32) == 0 && stdlib_alloc_seq() == seq_before + 1 &&
         ovr_v.capacity() == 32 && ovr_v.size() == 8;
    ovr_chk("ovr_vec_resize_size", ok);

    stdlib_telemetry_snapshot(&pre);
    stdlib_alloc_fail_reset();
    stdlib_alloc_fail_at(1);
    seq_before = stdlib_alloc_seq(); /* fail_at resets the sequence to 0 */
    ok = ovr_v.reserve(64) == -1 && stdlib_alloc_seq() == seq_before + 1;
    stdlib_alloc_fail_reset();
    stdlib_telemetry_snapshot(&post);
    ovr_chk("ovr_vec_resize_failure", ok && ovr_v.capacity() == 32 &&
                                          ovr_v.size() == 8 &&
                                          post.alloc_count == pre.alloc_count &&
                                          post.free_count == pre.free_count);
    {
        int v = -1;
        ok = ovr_v.at_checked(0, &v) == 0 && v == 0 &&
             ovr_v.at_checked(7, &v) == 0 && v == 7;
    }
    ovr_chk("ovr_vec_resize_contents", ok);
    ovr_v.drop();

    ok = ovr_s.reserve(16) == 0 && ovr_s.capacity() == 16;
    seq_before = stdlib_alloc_seq();
    ok = ok && ovr_s.append("defghijklmnopqrstuvwxyz") == 0 &&
         stdlib_alloc_seq() == seq_before + 1 && ovr_s.capacity() >= 27 &&
         ovr_s.size() == 26;
    ovr_chk("ovr_string_growth", ok && strcmp(ovr_s.c_str(),
                                              "abcdefghijklmnopqrstuvwxyz") == 0);
    ovr_s.drop();
}

int main(void) {
    stdlib_set_error_callback(ovr_cb);
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();
    ovr_cb_reset();
    ovr_realloc_sizes();
    ovr_growth();
    stdlib_set_error_callback(NULL);
    print("OVERFLOW_REALLOC", ovr_checks);
    print("OVERFLOW_REALLOC_FAILURES", ovr_failed);
    return ovr_failed == 0 ? 0 : 1;
}
