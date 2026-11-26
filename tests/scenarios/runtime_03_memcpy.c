/* Runtime scenarios for ORDINARY `memcpy` hardened by stdlib/safe/safe_c.h.
 *
 * Standalone C99.  Every scenario uses the ordinary C spelling `memcpy(d,s,n)`
 * (the safe_c.h macro maps it to cplus_memcpy_s_auto with a compile-time
 * destination capacity).  Acceptance is decided by `errno` (the auto wrapper
 * sets a non-zero code on every refusal path) AND by comparing the destination
 * against an independent byte-loop host reference.  Refusals are checked as
 * "non-zero errno + destination not overwritten by the copy".  No raw
 * `memcpy`/UB is ever invoked by this test: the reference is a plain loop.
 *
 * Build (both libs + includes, plain and UBSan): see verify notes at bottom.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_alloc.h"
#include "stdlib/safe/safe_memory.h"

#define N_SCENARIOS 25

/* Independent host reference: a byte loop, never a library memcpy.  Only called
 * for copies whose byte range is provably inside valid objects. */
static void ref_copy(unsigned char *d, const unsigned char *s, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) d[i] = s[i];
}

static void fill_pattern(unsigned char *p, size_t n, unsigned seed) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = (unsigned char)(seed + i * 13u);
}

static int bytes_eq(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; ++i)
        if (x[i] != y[i]) return 0;
    return 1;
}

/* Raw malloc helper: an untracked allocation is needed to exercise the
 * unknown-capacity policy.  FINDING: the recent-free cache is keyed by address
 * alone, so a raw malloc that reuses a recently freed tracked address is
 * falsely rejected as UAF.  Retry with a different size to avoid that confounder
 * and keep the scenarios aimed at capacity handling. */
static unsigned char *host_alloc(void) {
    unsigned tries;
    for (tries = 0; tries < 8; ++tries) {
        unsigned char *p = (unsigned char *)(malloc)(4096u + tries * 4096u);
        if (!p) return NULL;
        if (!cplus_safe_ptr_was_freed(p)) return p;
        (free)(p);
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */

/* SCENARIO 1: exact-size copy into a stack array; accepted, reference match. */
static int sc_03_exact_size_copy(void) {
    unsigned char src[16], dest[16], ref[16];
    size_t i;
    fill_pattern(src, sizeof src, 1u);
    for (i = 0; i < sizeof dest; ++i) dest[i] = 0xA5u;
    ref_copy(ref, src, sizeof src);
    errno = 0;
    memcpy(dest, src, sizeof src);
    if (errno != 0 || !bytes_eq(dest, ref, sizeof dest)) return 1;
    return 0;
}

/* SCENARIO 2: destination one byte short -> refused (ESNOSPC/ERANGE), no write. */
static int sc_03_dest_one_short(void) {
    unsigned char src[9], dest[8], initial[8];
    size_t i;
    fill_pattern(src, sizeof src, 2u);
    for (i = 0; i < sizeof dest; ++i) dest[i] = initial[i] = 0x3Cu;
    errno = 0;
    memcpy(dest, src, sizeof src); /* 9 > capacity 8 */
    if (errno != ERANGE || !bytes_eq(dest, initial, sizeof dest)) return 1;
    return 0;
}

/* SCENARIO 3: source nominally one byte short.  The auto wrapper checks the
 * proven source capacity (malloc_s tracks 16 bytes; src = blk + 12 leaves 4),
 * so a 5-byte request is refused with ERANGE and dest is left untouched.  The
 * host reference copies only the 4 in-bounds bytes: the test itself never
 * reads blk[16]. */
static int sc_03_src_one_short_gap(void) {
    unsigned char *blk = (unsigned char *)malloc_s(16);
    unsigned char dest[8], initial[8], ref[4];
    size_t i;
    const unsigned char *src;
    int bad = 0;
    if (!blk) return 1;
    fill_pattern(blk, 16, 3u);
    for (i = 0; i < sizeof dest; ++i) dest[i] = initial[i] = 0x5Au;
    src = blk + 12; /* proven remaining capacity is exactly 4 */
    if (cplus_safe_remaining_capacity(src) != 4) bad = 1;
    ref_copy(ref, src, sizeof ref); /* in-bounds host reference */
    errno = 0;
    memcpy(dest, src, 5); /* 5 > proven source capacity 4 -> refused */
    if (errno != ERANGE || !bytes_eq(dest, initial, sizeof dest)) bad = 1;
    free_s(blk);
    return bad;
}

/* SCENARIO 4: zero length is accepted and leaves the destination untouched. */
static int sc_03_zero_length(void) {
    unsigned char src[4], dest[4], initial[4];
    size_t i;
    fill_pattern(src, sizeof src, 4u);
    for (i = 0; i < sizeof dest; ++i) dest[i] = initial[i] = 0x11u;
    errno = 0;
    memcpy(dest, src, 0);
    if (errno != 0 || !bytes_eq(dest, initial, sizeof dest)) return 1;
    return 0;
}

/* SCENARIO 5: NULL destination refused. */
static int sc_03_null_dest(void) {
    unsigned char src[4];
    fill_pattern(src, sizeof src, 5u);
    errno = 0;
    memcpy(NULL, src, sizeof src);
    if (errno != EINVAL) return 1;
    return 0;
}

/* SCENARIO 6: NULL source refused; destination untouched. */
static int sc_03_null_src(void) {
    unsigned char dest[4], initial[4];
    size_t i;
    for (i = 0; i < sizeof dest; ++i) dest[i] = initial[i] = 0x22u;
    errno = 0;
    memcpy(dest, NULL, sizeof dest);
    if (errno != EINVAL || !bytes_eq(dest, initial, sizeof dest)) return 1;
    return 0;
}

/* SCENARIO 7: two adjacent, non-overlapping struct members; accepted.  Using
 * member arrays keeps each side's proven capacity exact (8) so the full-capacity
 * overlap test does not produce a false positive. */
static int sc_03_adjacent_buffers(void) {
    struct pair_s { unsigned char d[8]; unsigned char s[8]; } p;
    unsigned char ref[8];
    size_t i;
    fill_pattern(p.s, sizeof p.s, 7u);
    ref_copy(ref, p.s, sizeof ref);
    errno = 0;
    memcpy(p.d, p.s, sizeof ref);
    if (errno != 0 || !bytes_eq(p.d, ref, sizeof ref)) return 1;
    for (i = 0; i < sizeof p.d; ++i)
        if (p.d[i] != ref[i]) return 1;
    return 0;
}

/* SCENARIO 8: overlapping forward copy (dest below src) refused.  The C+
 * memcpy_s/security patch deliberately leaves [dest, dest+dmax) intact on
 * ESOVRLP (locked in tests/crt/adversarial/test_mem50_memmove_overlap.c), so
 * the whole buffer must still match its pre-call snapshot. */
static int sc_03_overlap_forward(void) {
    unsigned char buf[16], initial[16];
    size_t i;
    fill_pattern(buf, sizeof buf, 8u);
    for (i = 0; i < sizeof buf; ++i) initial[i] = buf[i];
    errno = 0;
    memcpy(buf, buf + 2, 8);
    if (errno != EINVAL || !bytes_eq(buf, initial, sizeof buf)) return 1;
    return 0;
}

/* SCENARIO 9: overlapping backward copy (dest above src) refused; dest is
 * left intact just as in scenario 8 (no error-path zeroing). */
static int sc_03_overlap_backward(void) {
    unsigned char buf[16], initial[16];
    size_t i;
    fill_pattern(buf, sizeof buf, 9u);
    for (i = 0; i < sizeof buf; ++i) initial[i] = buf[i];
    errno = 0;
    memcpy(buf + 4, buf, 8);
    if (errno != EINVAL || !bytes_eq(buf, initial, sizeof buf)) return 1;
    return 0;
}

/* SCENARIO 10: self-copy (dest == src) is explicitly allowed; reference match. */
static int sc_03_self_copy(void) {
    unsigned char buf[16], ref[16];
    fill_pattern(buf, sizeof buf, 10u);
    ref_copy(ref, buf, sizeof ref);
    errno = 0;
    memcpy(buf, buf, sizeof buf);
    if (errno != 0 || !bytes_eq(buf, ref, sizeof buf)) return 1;
    return 0;
}

/* SCENARIO 11: interior heap destination with insufficient remaining capacity
 * (2 bytes left, 4 requested) refused; trailing bytes unchanged. */
static int sc_03_interior_dest_short(void) {
    unsigned char *blk = (unsigned char *)malloc_s(16);
    unsigned char src[4], snap[2];
    int bad = 0;
    if (!blk) return 1;
    fill_pattern(blk, 16, 11u);
    fill_pattern(src, sizeof src, 12u);
    snap[0] = blk[14]; snap[1] = blk[15];
    errno = 0;
    memcpy(blk + 14, src, 4);
    if (errno != ERANGE || blk[14] != snap[0] || blk[15] != snap[1]) bad = 1;
    free_s(blk);
    return bad;
}

/* SCENARIO 12: struct member destination with a trailing canary; exact-length
 * copy accepted and canary intact. */
static int sc_03_struct_canary_ok(void) {
    struct canary_s { unsigned char buf[8]; unsigned long canary; } c;
    unsigned char src[8], ref[8];
    size_t i;
    for (i = 0; i < sizeof c.buf; ++i) c.buf[i] = 0x44u;
    c.canary = 0xDEADBEEFCAFEF00DUL;
    fill_pattern(src, sizeof src, 12u);
    ref_copy(ref, src, sizeof ref);
    errno = 0;
    memcpy(c.buf, src, sizeof src);
    if (errno != 0 || !bytes_eq(c.buf, ref, sizeof c.buf)) return 1;
    if (c.canary != 0xDEADBEEFCAFEF00DUL) return 1;
    return 0;
}

/* SCENARIO 13: same struct, one byte too many; refused, canary and buf intact. */
static int sc_03_struct_canary_overflow(void) {
    struct canary_s { unsigned char buf[8]; unsigned long canary; } c;
    unsigned char src[9], initial[8];
    size_t i;
    for (i = 0; i < sizeof c.buf; ++i) c.buf[i] = initial[i] = 0x44u;
    c.canary = 0xDEADBEEFCAFEF00DUL;
    fill_pattern(src, sizeof src, 13u);
    errno = 0;
    memcpy(c.buf, src, sizeof src); /* 9 > 8 */
    if (errno != ERANGE || !bytes_eq(c.buf, initial, sizeof initial)) return 1;
    if (c.canary != 0xDEADBEEFCAFEF00DUL) return 1;
    return 0;
}

/* SCENARIO 14: global (static storage) destination; accepted, reference match. */
static unsigned char g_dest[24];

static int sc_03_global_dest(void) {
    unsigned char src[24], ref[24];
    size_t i;
    fill_pattern(src, sizeof src, 14u);
    for (i = 0; i < sizeof g_dest; ++i) g_dest[i] = 0x77u;
    ref_copy(ref, src, sizeof ref);
    errno = 0;
    memcpy(g_dest, src, sizeof src);
    if (errno != 0 || !bytes_eq(g_dest, ref, sizeof g_dest)) return 1;
    return 0;
}

/* SCENARIO 15: SIZE_MAX length on an untracked host buffer; refused because
 * n > RSIZE_MAX_MEM, with no memory touched.  FINDING: the refusal diagnostic
 * prints "requested X > capacity X" here (dmax was set to n by the unknown-
 * capacity fallback), so the message alone misidentifies the reason. */
static int sc_03_size_max_length(void) {
    unsigned char *host = host_alloc(); /* bypass malloc_s tracking */
    unsigned char src[8], initial[8];
    size_t i;
    int bad = 0;
    if (!host) return 1;
    for (i = 0; i < 8; ++i) host[i] = initial[i] = 0x88u;
    fill_pattern(src, sizeof src, 15u);
    errno = 0;
    memcpy(host, src, SIZE_MAX);
    if (errno != ERANGE || !bytes_eq(host, initial, 8)) bad = 1;
    (free)(host);
    return bad;
}

/* SCENARIO 16: source from raw host malloc (unknown capacity).  Default policy
 * preserves raw-C behaviour with a warning; copy is accepted and matches ref. */
static int sc_03_host_malloc_unknown(void) {
    unsigned char *host = host_alloc();
    unsigned char src[8], ref[8];
    size_t i;
    int bad = 0;
    if (!host) return 1;
    for (i = 0; i < 8; ++i) host[i] = 0x99u;
    fill_pattern(src, sizeof src, 16u);
    ref_copy(ref, src, sizeof ref);
    errno = 0;
    memcpy(host, src, sizeof ref);
    if (errno != 0 || !bytes_eq(host, ref, sizeof ref)) bad = 1;
    (free)(host);
    return bad;
}

/* SCENARIO 17: strict unknown-capacity mode refuses the same raw-host copy. */
static int sc_03_strict_unknown_refused(void) {
    unsigned char *host = host_alloc();
    unsigned char src[8], initial[8];
    size_t i;
    int bad = 0;
    if (!host) return 1;
    for (i = 0; i < 8; ++i) host[i] = initial[i] = 0x9Au;
    fill_pattern(src, sizeof src, 17u);
    cplus_safe_set_unknown_capacity_mode(1);
    errno = 0;
    memcpy(host, src, sizeof src);
    if (errno != EINVAL || !bytes_eq(host, initial, 8)) bad = 1;
    cplus_safe_set_unknown_capacity_mode(0);
    (free)(host);
    return bad;
}

/* SCENARIO 18: source pointer freed by free_s -> UAF refused (EFAULT). */
static int sc_03_source_uaf(void) {
    unsigned char *p = (unsigned char *)malloc_s(8);
    unsigned char dest[8], initial[8];
    size_t i;
    int bad = 0;
    if (!p) return 1;
    for (i = 0; i < 8; ++i) p[i] = 0xABu;
    for (i = 0; i < sizeof dest; ++i) dest[i] = initial[i] = 0xBCu;
    free_s(p); /* no allocation between free and the copy: recent cache is intact */
    errno = 0;
    memcpy(dest, p, sizeof dest);
    if (errno != EFAULT || !bytes_eq(dest, initial, sizeof dest)) bad = 1;
    return bad;
}

/* SCENARIO 19: destination pointer freed by free_s -> UAF refused (EFAULT).
 * The freed block is never dereferenced by the test. */
static int sc_03_dest_uaf(void) {
    unsigned char *p = (unsigned char *)malloc_s(8);
    unsigned char src[8];
    fill_pattern(src, sizeof src, 19u);
    if (!p) return 1;
    free_s(p);
    errno = 0;
    memcpy(p, src, sizeof src);
    if (errno != EFAULT) return 1;
    return 0;
}

/* SCENARIO 20: stale alias after realloc moves the block.  The old pointer is
 * in the recent-free cache and is refused; the new pointer copies correctly. */
static int sc_03_realloc_move_stale(void) {
    unsigned char *a = (unsigned char *)malloc_s(16);
    unsigned char *b;
    unsigned char src[16], ref[16];
    int bad = 0;
    if (!a) return 1;
    fill_pattern(a, 16, 20u);
    /* FINDING: reallocating to >16KB makes the live copy fail too, because the
     * auto wrapper passes the full remaining capacity as dmax to safeclib,
     * whose RSIZE_MAX_MEM is 16KB.  Use 4KB here to keep this scenario about
     * the stale alias. */
    b = (unsigned char *)realloc_s(a, 4096);
    if (!b) return 1;
    if (b == a) { /* allocator kept the address: no stale alias to test */
        free_s(b);
        return 0;
    }
    fill_pattern(src, sizeof src, 21u);
    ref_copy(ref, src, sizeof ref);
    errno = 0;
    memcpy(a, src, sizeof src); /* stale alias */
    if (errno != EFAULT) bad = 1;
    errno = 0;
    memcpy(b, src, sizeof src); /* live alias */
    if (errno != 0 || !bytes_eq(b, ref, sizeof ref)) bad = 1;
    free_s(b);
    return bad;
}

/* SCENARIO 21: realloc shrink leaves a smaller tracked capacity; the old
 * length is refused, the new length is accepted and matches the reference. */
static int sc_03_realloc_shrink(void) {
    unsigned char *p = (unsigned char *)malloc_s(32);
    unsigned char *q;
    unsigned char src[32], snap[8], ref[8];
    size_t i;
    int bad = 0;
    if (!p) return 1;
    fill_pattern(p, 32, 22u);
    fill_pattern(src, sizeof src, 23u);
    for (i = 0; i < sizeof snap; ++i) snap[i] = p[i];
    q = (unsigned char *)realloc_s(p, 8);
    if (!q) return 1;
    errno = 0;
    memcpy(q, src, 32); /* 32 > new capacity 8 */
    if (errno != ERANGE || !bytes_eq(q, snap, sizeof snap)) bad = 1;
    ref_copy(ref, src, sizeof ref);
    errno = 0;
    memcpy(q, src, sizeof ref);
    if (errno != 0 || !bytes_eq(q, ref, sizeof ref)) bad = 1;
    free_s(q);
    return bad;
}

/* SCENARIO 22: source with no NUL byte; explicit length must copy every byte,
 * including the last one (no string-style stop). */
static int sc_03_no_nul_explicit_len(void) {
    const unsigned char src[5] = { 1u, 2u, 3u, 4u, 5u };
    unsigned char dest[8], ref[8];
    size_t i;
    for (i = 0; i < sizeof dest; ++i) dest[i] = 0xEEu;
    ref_copy(ref, src, sizeof src);
    errno = 0;
    memcpy(dest, src, sizeof src);
    if (errno != 0 || !bytes_eq(dest, ref, sizeof src)) return 1;
    for (i = sizeof src; i < sizeof dest; ++i)
        if (dest[i] != 0xEEu) return 1;
    return 0;
}

/* SCENARIO 23: length just above RSIZE_MAX_MEM refused; no memory touched. */
static int sc_03_near_rsize_max(void) {
    unsigned char *host = host_alloc();
    unsigned char src[8], initial[8];
    size_t i;
    int bad = 0;
    if (!host) return 1;
    for (i = 0; i < 8; ++i) host[i] = initial[i] = 0xCDu;
    fill_pattern(src, sizeof src, 24u);
    errno = 0;
    memcpy(host, src, RSIZE_MAX_MEM + 1u);
    if (errno != ERANGE || !bytes_eq(host, initial, 8)) bad = 1;
    (free)(host);
    return bad;
}

/* SCENARIO 24: logging OFF vs TRACE must produce identical results. */
static int sc_03_logging_parity(void) {
    unsigned char src[8], dest[8], ref[8];
    const cplus_safe_log_level saved = cplus_safe_log_get_level();
    int e1, e2, ok1, ok2;
    fill_pattern(src, sizeof src, 25u);
    ref_copy(ref, src, sizeof ref);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    fill_pattern(dest, sizeof dest, 0u);
    errno = 0;
    memcpy(dest, src, sizeof src);
    e1 = errno; ok1 = bytes_eq(dest, ref, sizeof dest);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE);
    fill_pattern(dest, sizeof dest, 0u);
    errno = 0;
    memcpy(dest, src, sizeof src);
    e2 = errno; ok2 = bytes_eq(dest, ref, sizeof dest);
    cplus_safe_log_set_level(saved);
    if (e1 != 0 || e2 != 0 || !ok1 || !ok2) return 1;
    return 0;
}

/* SCENARIO 25: many repeated copies into one tracked heap destination; each
 * iteration must match its own host reference. */
static int sc_03_repeat_loop(void) {
    unsigned char *dest = (unsigned char *)malloc_s(64);
    unsigned char ref[64];
    size_t iter, i;
    int bad = 0;
    if (!dest) return 1;
    for (iter = 0; iter < 64; ++iter) {
        for (i = 0; i < sizeof ref; ++i) ref[i] = (unsigned char)(iter * 3u + i);
        errno = 0;
        memcpy(dest, ref, sizeof ref);
        if (errno != 0 || !bytes_eq(dest, ref, sizeof ref)) { bad = 1; break; }
    }
    free_s(dest);
    return bad;
}

int main(void) {
    int failed = 0;

    /* Recoverable refusals, not fatal exits. */
    cplus_runtime_set_violation_trap(0);
    cplus_safe_set_unknown_capacity_mode(0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);

    failed += sc_03_exact_size_copy();
    failed += sc_03_dest_one_short();
    failed += sc_03_src_one_short_gap();
    failed += sc_03_zero_length();
    failed += sc_03_null_dest();
    failed += sc_03_null_src();
    failed += sc_03_adjacent_buffers();
    failed += sc_03_overlap_forward();
    failed += sc_03_overlap_backward();
    failed += sc_03_self_copy();
    failed += sc_03_interior_dest_short();
    failed += sc_03_struct_canary_ok();
    failed += sc_03_struct_canary_overflow();
    failed += sc_03_global_dest();
    failed += sc_03_size_max_length();
    failed += sc_03_host_malloc_unknown();
    failed += sc_03_strict_unknown_refused();
    failed += sc_03_source_uaf();
    failed += sc_03_dest_uaf();
    failed += sc_03_realloc_move_stale();
    failed += sc_03_realloc_shrink();
    failed += sc_03_no_nul_explicit_len();
    failed += sc_03_near_rsize_max();
    failed += sc_03_logging_parity();
    failed += sc_03_repeat_loop();

    if (failed != 0) {
        printf("runtime_memcpy: %d of %d scenarios failed\n", failed, N_SCENARIOS);
        return 1;
    }
    printf("runtime_memcpy: %d scenarios ok\n", N_SCENARIOS);
    return 0;
}

/* Verify (from repo root), plain and UBSan:
 *   clang -O2 -std=c99 -Wall -Wextra -pedantic \
 *     -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
 *     -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
 *     tests/scenarios/runtime_03_memcpy.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -lm -pthread \
 *     -o build/scenarios/runtime_03_memcpy_plain && \
 *     build/scenarios/runtime_03_memcpy_plain
 * Add: -fsanitize=undefined -fno-sanitize-recover=all \
 *      -fno-sanitize=float-divide-by-zero  for the UBSan build.
 */
