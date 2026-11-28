/* Phase 5: alignment rounding, header+payload bookkeeping, SafeCRT limits.
 *
 * Default build (no extra flags): checks that the checked allocator's size
 * arithmetic records exactly the requested payload (no hidden header) and
 * returns naturally aligned blocks for boundary sizes 1..4096, that the
 * metadata side table grows without arithmetic wrap, and that fixed-size
 * list nodes (prev/next header + int payload) need no size multiplication.
 *
 * Optional SafeCRT build (-DSTDLIB_OVERFLOW_SAFECRT -I. plus
 * build/safe/libcplus_safecrt.a): adds overflow/limit probes for
 * malloc_s/calloc_s/realloc_s/aligned_alloc_s.  Those wrappers validate size
 * and alignment BEFORE consuming cplus_safe_alloc_seq(), so a rejection must
 * leave the sequence counter and errno-documented state unchanged.  Without
 * the macro this section prints an explicit SKIP so the file still builds in
 * the plain two-mode harness.
 *
 * Self-checking: "<tag> <0|1>", final "OVERFLOW_ALIGNMENT <n> <f>".
 */
#ifndef STDLIB_ALLOC_LIMIT
#define STDLIB_ALLOC_LIMIT ((size_t)4096)
#endif

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

#ifdef STDLIB_OVERFLOW_SAFECRT
#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_alloc.h"
#endif

#define OVA_LIMIT ((size_t)4096)

static int ova_checks = 0;
static int ova_failed = 0;

static void ova_chk(const char *tag, int cond) {
    ova_checks++;
    if (!cond)
        ova_failed++;
    print(tag, cond ? 1 : 0);
}

/* ---- stdlib side: payload size, alignment, side-table growth ----------- */
static void ova_payload_alignment(void) {
    static const size_t sizes[6] = {1u, 7u, 8u, 63u, 64u, OVA_LIMIT};
    void *p;
    int i, ok = 1;

    for (i = 0; i < 6; i++) {
        p = stdlib_alloc(sizes[i]);
        ok = ok && p != NULL &&
             (((uintptr_t)p % (uintptr_t)_Alignof(max_align_t)) == 0) &&
             stdlib_alloc_size(p) == sizes[i];
        if (p != NULL)
            stdlib_release(p);
    }
    ova_chk("ova_payload_alignment", ok);

    p = stdlib_alloc(0);
    ova_chk("ova_zero_payload", p != NULL && stdlib_alloc_size(p) == 1);
    if (p != NULL)
        stdlib_release(p);

    p = stdlib_calloc(511, 8u);
    ova_chk("ova_calloc_payload", p != NULL && stdlib_alloc_size(p) == 4088 &&
                                      stdlib_alloc_elem_size(p) == 8u &&
                                      stdlib_alloc_length(p) == 511);
    if (p != NULL)
        stdlib_release(p);
}

/* Force the metadata table through 64 -> 128 -> 256 growth; every recorded
 * header must still describe the exact payload. */
static void ova_meta_growth(void) {
    void *blocks[200];
    stdlib_telemetry_stats pre, post;
    int i, ok = 1;

    stdlib_telemetry_snapshot(&pre);
    for (i = 0; i < 200; i++) {
        blocks[i] = stdlib_alloc((size_t)(i + 1));
        ok = ok && blocks[i] != NULL && stdlib_alloc_size(blocks[i]) == (size_t)(i + 1);
    }
    ova_chk("ova_meta_record", ok);
    for (i = 0; i < 200; i++)
        stdlib_release(blocks[i]);
    stdlib_telemetry_snapshot(&post);
    ova_chk("ova_meta_balanced",
            post.alloc_count - pre.alloc_count ==
                post.free_count - pre.free_count);
}

/* list_node is a fixed-size header (prev/next) + int payload: one
 * stdlib_alloc(sizeof(node)) per push, no count multiplication. */
static void ova_node_size(void) {
    list ovl_nodes = list();
    stdlib_telemetry_stats pre, post;
    int i, ok = 1, val = -1;

    stdlib_telemetry_snapshot(&pre);
    for (i = 0; i < 200; i++)
        ok = ok && ovl_nodes.push_back(i) == 0;
    ok = ok && ovl_nodes.size() == 200;
    for (i = 0; i < 200; i++)
        ok = ok && ovl_nodes.pop_front(&val) == 0 && val == i;
    stdlib_telemetry_snapshot(&post);
    ova_chk("ova_node_fixed_size", ok && ovl_nodes.size() == 0 &&
                                       post.alloc_count - pre.alloc_count ==
                                           post.free_count - pre.free_count);
    ovl_nodes.drop();
}

#ifdef STDLIB_OVERFLOW_SAFECRT
/* ---- SafeCRT side: size/alignment validation before allocation -------- */
static void ova_safecrt_limits(void) {
    unsigned long seq_before;
    size_t live_before;
    void *p;
    void *block;
    int ok;

    seq_before = cplus_safe_alloc_seq();
    live_before = cplus_safe_alloc_live_count();

    errno = 0;
    p = malloc_s(SIZE_MAX);
    ok = p == NULL && errno == EINVAL && cplus_safe_alloc_seq() == seq_before;
    ova_chk("ova_malloc_s_sxmax", ok);

    errno = 0;
    p = malloc_s((SIZE_MAX >> 1) + 1u);
    ok = p == NULL && errno == EINVAL && cplus_safe_alloc_seq() == seq_before;
    ova_chk("ova_malloc_s_over_rsizemax", ok);

    errno = 0;
    p = calloc_s(SIZE_MAX, 4u);
    ok = p == NULL && errno == EOVERFLOW && cplus_safe_alloc_seq() == seq_before;
    ova_chk("ova_calloc_s_mul_sxmax", ok);

    errno = 0;
    p = calloc_s(2u, SIZE_MAX);
    ok = p == NULL && errno == EOVERFLOW && cplus_safe_alloc_seq() == seq_before;
    ova_chk("ova_calloc_s_mul_reverse", ok);

    p = calloc_s(0u, SIZE_MAX);
    ok = p != NULL && cplus_safe_ptr_size(p) == 1u;
    ova_chk("ova_calloc_s_zero_count", ok);
    if (p != NULL)
        free_s(p);

    block = malloc_s(64u);
    ova_chk("ova_safecrt_block", block != NULL && cplus_safe_ptr_size(block) == 64u);
    if (block != NULL) {
        memset(block, 0x5a, 64u);
        errno = 0;
        seq_before = cplus_safe_alloc_seq();
        p = realloc_s(block, SIZE_MAX);
        ok = p == NULL && errno == EINVAL &&
             cplus_safe_alloc_seq() == seq_before &&
             cplus_safe_ptr_size(block) == 64u &&
             ((unsigned char *)block)[0] == 0x5a;
        ova_chk("ova_realloc_s_over_rsizemax", ok);
        free_s(block);
        seq_before = cplus_safe_alloc_seq();
    }

    errno = 0;
    p = aligned_alloc_s(0u, 16u);
    ok = p == NULL && errno == EINVAL && cplus_safe_alloc_seq() == seq_before;
    ova_chk("ova_aligned_s_align0", ok);
    errno = 0;
    p = aligned_alloc_s(3u, 16u);
    ok = p == NULL && errno == EINVAL && cplus_safe_alloc_seq() == seq_before;
    ova_chk("ova_aligned_s_align3", ok);
    errno = 0;
    p = aligned_alloc_s(SIZE_MAX, 16u);
    ok = p == NULL && errno == EINVAL && cplus_safe_alloc_seq() == seq_before;
    ova_chk("ova_aligned_s_align_sxmax", ok);
    errno = 0;
    p = aligned_alloc_s(16u, SIZE_MAX);
    ok = p == NULL && errno == EINVAL && cplus_safe_alloc_seq() == seq_before;
    ova_chk("ova_aligned_s_size_sxmax", ok);

    p = aligned_alloc_s(16u, 64u);
    ok = p != NULL && (((uintptr_t)p & 15u) == 0) && cplus_safe_ptr_size(p) == 64u;
    ova_chk("ova_aligned_s_ok", ok);
    if (p != NULL)
        free_s(p);

    ova_chk("ova_safecrt_live_balance",
            cplus_safe_alloc_live_count() == live_before);
}
#else
static void ova_safecrt_limits(void) {
    print("SKIP SafeCRT overflow probes: build with -DSTDLIB_OVERFLOW_SAFECRT "
          "-I. build/safe/libcplus_safecrt.a");
}
#endif

int main(void) {
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();
    ova_payload_alignment();
    ova_meta_growth();
    ova_node_size();
    ova_safecrt_limits();
    print("OVERFLOW_ALIGNMENT", ova_checks);
    print("OVERFLOW_ALIGNMENT_FAILURES", ova_failed);
    return ova_failed == 0 ? 0 : 1;
}
