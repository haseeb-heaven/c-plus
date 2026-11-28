/* runtime_04_memmove_memcmp.c
 *
 * 26 DISTINCT ordinary-C runtime scenarios for `memmove` and `memcmp` after
 * hardening by stdlib/safe/safe_c.h.  The ordinary spellings are used for the
 * data operations; `cplus_memmove_s_auto` / `cplus_memcmp_s_auto` are called
 * directly where the *return value* must be observed (the `memmove` macro in
 * safe_c.h void-casts its result, while `memcmp` does not).  Both are the exact
 * functions the ordinary macros expand to.
 *
 * Host reference: __builtin_memmove / __builtin_memcmp bypass the macros and
 * provide the C library ground truth.  memcmp_s normalises its result to
 * -1/0/1, so comparisons against the host use the SIGN only (which is all ISO C
 * specifies for memcmp).
 *
 * C99, main returns 0.  The SafeCRT violation trap is disabled so refused
 * operations return a value instead of terminating; no scenario performs a
 * real out-of-bounds access (the wrappers refuse before dereferencing).
 */
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_stats.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_alloc.h"
#include "libc/install-include/cplus_libc.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- host-reference helpers (not affected by the safe_c.h macros) ------ */
static void *host_memmove(void *d, const void *s, size_t n) {
    return __builtin_memmove(d, s, n);
}
static int host_memcmp(const void *a, const void *b, size_t n) {
    return __builtin_memcmp(a, b, n);
}
static int sign_of(int v) { return (v > 0) - (v < 0); }
static int bytes_eq(const void *a, const void *b, size_t n) {
    return __builtin_memcmp(a, b, n) == 0;
}
static void fill_seq(unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = (unsigned char)(i + 1);
}

static unsigned char g_global_dst[16]; /* scenario 09 uses a static object */

/* Holds a stack pointer behind a volatile load so __builtin_object_size (and
 * the SafeCRT allocation table) cannot prove its capacity: the capacity is
 * genuinely UNKNOWN.  A stack address also cannot collide with the allocator's
 * recent-free cache, so this is not misreported as a use-after-free. */
static volatile unsigned char *g_opaque_ptr;
#if defined(__clang__) || defined(__GNUC__)
#define RMMC_NOINLINE __attribute__((noinline))
#else
#define RMMC_NOINLINE
#endif
static RMMC_NOINLINE void rmmc_opaque_store(unsigned char *p) {
    g_opaque_ptr = p;
}

static int g_scenarios;
#define RUN(fn) do { (fn)(); ++g_scenarios; } while (0)

/* =========================== memmove scenarios ========================== */

/* 01: overlapping move where dest < src ("left-to-right" copy direction). */
static void sc_04_memmove_overlap_left(void) {
    unsigned char a[16], b[16];
    void *r;
    fill_seq(a, sizeof(a));
    fill_seq(b, sizeof(b));
    r = cplus_memmove_s_auto(a, a + 2, 4, CPLUS_SAFE_OBJ_SIZE(a));
    host_memmove(b, b + 2, 4);
    assert(r == a);
    assert(bytes_eq(a, b, sizeof(a))); /* a == {3,4,5,6,5,6,7,...} */
}

/* 02: overlapping move where dest > src ("right-to-left" copy direction). */
static void sc_04_memmove_overlap_right(void) {
    unsigned char a[16], b[16];
    fill_seq(a, sizeof(a));
    fill_seq(b, sizeof(b));
    (void)memmove(a + 2, a, 4); /* ordinary spelling */
    host_memmove(b + 2, b, 4);
    assert(bytes_eq(a, b, sizeof(a)));
}

/* 03: exactly adjacent, non-overlapping regions (dest starts at src end). */
static void sc_04_memmove_adjacent(void) {
    unsigned char a[12], b[12];
    fill_seq(a, sizeof(a));
    fill_seq(b, sizeof(b));
    (void)memmove(a + 6, a, 6);
    host_memmove(b + 6, b, 6);
    assert(bytes_eq(a, b, sizeof(a)));
    assert(a[0] == 1 && a[5] == 6 && a[6] == 1 && a[11] == 6);
}

/* 04: self-move (dest == src) must leave the object byte-for-byte intact. */
static void sc_04_memmove_self(void) {
    unsigned char a[8], b[8];
    fill_seq(a, sizeof(a));
    fill_seq(b, sizeof(b));
    (void)memmove(a, a, sizeof(a));
    host_memmove(b, b, sizeof(b));
    assert(bytes_eq(a, b, sizeof(a)));
    assert(a[0] == 1 && a[7] == 8);
}

/* 05: zero-length move returns dest and changes nothing. */
static void sc_04_memmove_zero_len(void) {
    unsigned char a[4] = {9, 8, 7, 6}, b[4] = {1, 2, 3, 4};
    void *r = cplus_memmove_s_auto(a, b, 0, CPLUS_SAFE_OBJ_SIZE(a));
    (void)memmove(a, b, 0); /* ordinary spelling, must also be a no-op */
    assert(r == a);
    assert(bytes_eq(a, "\x09\x08\x07\x06", 4));
}

/* 06: NULL arguments are refused with EINVAL (trap disabled). */
static void sc_04_memmove_null_args(void) {
    unsigned char src[4] = {1, 2, 3, 4};
    errno = 0;
    (void)memmove(NULL, src, 4); /* ordinary spelling, refused */
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_memmove_s_auto(NULL, src, 4, (size_t)-1) == NULL);
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_memmove_s_auto(src, NULL, 4, CPLUS_SAFE_OBJ_SIZE(src)) == NULL);
    assert(errno == EINVAL);
    assert(src[0] == 1 && src[3] == 4); /* nothing written */
}

/* 07: interior heap dest with insufficient REMAINING capacity is refused and
 *     must not corrupt the containing block. */
static void sc_04_memmove_interior_insufficient(void) {
    unsigned char *base, snapshot[16];
    unsigned char src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    base = (unsigned char *)malloc_s(16);
    assert(base != NULL);
    fill_seq(base, 16);
    host_memmove(snapshot, base, 16);
    errno = 0;
    (void)memmove(base + 10, src, 8); /* 8 > remaining 6 -> refused */
    assert(errno == ERANGE);
    assert(cplus_safe_ptr_size(base) == 16);
    assert(bytes_eq(base, snapshot, 16)); /* block untouched */
    free_s(base);
}

/* 08: stack destination, including a non-zero source offset. */
static void sc_04_memmove_stack_dest(void) {
    unsigned char a[10], b[10];
    fill_seq(a, sizeof(a));
    fill_seq(b, sizeof(b));
    (void)memmove(a + 1, a + 4, 5);
    host_memmove(b + 1, b + 4, 5);
    assert(bytes_eq(a, b, sizeof(a)));
}

/* 09: global/static destination. */
static void sc_04_memmove_global_dest(void) {
    unsigned char ref[sizeof(g_global_dst)];
    fill_seq(g_global_dst, sizeof(g_global_dst));
    fill_seq(ref, sizeof(ref));
    (void)memmove(g_global_dst, g_global_dst + 8, 8);
    host_memmove(ref, ref + 8, 8);
    assert(bytes_eq(g_global_dst, ref, sizeof(g_global_dst)));
}

/* 10: struct field destination (object size is the field, not the struct). */
static void sc_04_memmove_struct_dest(void) {
    struct rec { int tag; unsigned char data[8]; int tail; } s, t;
    unsigned char src[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    s.tag = 11; s.tail = 22;
    t = s;
    (void)memmove(s.data, src, sizeof(src));
    host_memmove(t.data, src, sizeof(src));
    assert(bytes_eq(s.data, t.data, sizeof(s.data)));
    assert(s.tag == 11 && s.tail == 22); /* neighbouring fields intact */
    assert(s.data[0] == 8 && s.data[7] == 1);
}

/* 11: SIZE_MAX length is rejected as exceeding RSIZE_MAX_MEM/capacity. */
static void sc_04_memmove_size_max(void) {
    unsigned char a[4] = {1, 2, 3, 4};
    errno = 0;
    (void)memmove(a, a + 1, SIZE_MAX);
    assert(errno == ERANGE);
    assert(cplus_memmove_s_auto(a, a + 1, SIZE_MAX, CPLUS_SAFE_OBJ_SIZE(a)) == NULL);
    assert(a[0] == 1 && a[1] == 2 && a[2] == 3 && a[3] == 4);
}

/* 12: use-after-free (post-free alias) is refused, no dereference occurs. */
static void sc_04_memmove_uaf_refused(void) {
    unsigned char *p = (unsigned char *)malloc_s(16);
    unsigned char src[4] = {5, 6, 7, 8};
    assert(p != NULL);
    fill_seq(p, 16);
    free_s(p);
    errno = 0;
    (void)memmove(p, src, 4); /* ordinary spelling, must be refused */
    assert(errno == EFAULT);
    assert(cplus_memmove_s_auto(p, src, 4, (size_t)-1) == NULL);
    assert(cplus_safe_ptr_was_freed(p) == 1);
}

/* 13: post-realloc stale alias: generation changes, old pointer is not
 *     dereferenced; the NEW pointer with its NEW capacity is used safely. */
static void sc_04_memmove_realloc_stale_alias(void) {
    unsigned char *p = (unsigned char *)malloc_s(32);
    unsigned char *q, ref[64];
    cplus_safe_alloc_meta meta;
    unsigned long long g0;
    size_t i;
    assert(p != NULL);
    fill_seq(p, 32);
    assert(cplus_safe_alloc_get_meta(p, &meta) == CPLUS_SAFE_OK);
    g0 = meta.generation;
    q = (unsigned char *)realloc_s(p, 64);
    assert(q != NULL);
    assert(cplus_safe_ptr_check_generation(q, g0) == 0); /* alias is stale */
    assert(cplus_safe_ptr_size(q) == 64);
    fill_seq(q, 64); /* initialise the whole NEW capacity */
    fill_seq(ref, 64);
    host_memmove(ref, ref + 8, 24);
    (void)memmove(q, q + 8, 24); /* valid via the new pointer/capacity */
    assert(bytes_eq(q, ref, 64));
    for (i = 0; i < 24; ++i) assert(q[i] == (unsigned char)(i + 9));
    for (i = 24; i < 32; ++i) assert(q[i] == (unsigned char)(i + 1));
    free_s(q);
}

/* 14: move of exactly the full capacity (boundary) must be accepted. */
static void sc_04_memmove_heap_exact_capacity(void) {
    unsigned char *a = (unsigned char *)malloc_s(16);
    unsigned char *b = (unsigned char *)malloc_s(16);
    size_t i;
    assert(a != NULL && b != NULL);
    for (i = 0; i < 16; ++i) { a[i] = (unsigned char)i; b[i] = (unsigned char)i; }
    assert(cplus_memmove_s_auto(a, b, 16, cplus_safe_ptr_size(a)) == a);
    assert(bytes_eq(a, b, 16));
    /* one byte more must be refused */
    errno = 0;
    assert(cplus_memmove_s_auto(a, b, 17, cplus_safe_ptr_size(a)) == NULL);
    assert(errno == ERANGE);
    free_s(a);
    free_s(b);
}

/* =========================== memcmp scenarios =========================== */

/* 15: equal buffers -> 0, in sign agreement with the host. */
static void sc_04_memcmp_equal(void) {
    unsigned char a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    unsigned char b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    int r = memcmp(a, b, 8); /* ordinary spelling */
    assert(r == 0 && host_memcmp(a, b, 8) == 0);
}

/* 16: first-byte difference determines the sign. */
static void sc_04_memcmp_first_byte_diff(void) {
    unsigned char a[4] = {1, 9, 9, 9};
    unsigned char b[4] = {2, 0, 0, 0};
    assert(sign_of(memcmp(a, b, 4)) == sign_of(host_memcmp(a, b, 4)));
    assert(memcmp(a, b, 4) < 0);
    assert(memcmp(b, a, 4) > 0);
}

/* 17: difference only in the final byte is still found. */
static void sc_04_memcmp_last_byte_diff(void) {
    unsigned char a[6] = {7, 7, 7, 7, 7, 3};
    unsigned char b[6] = {7, 7, 7, 7, 7, 9};
    int r = memcmp(a, b, 6);
    assert(sign_of(r) == sign_of(host_memcmp(a, b, 6)));
    assert(r < 0 && memcmp(b, a, 6) > 0);
    assert(memcmp(a, b, 5) == 0); /* outside explicit n -> equal */
}

/* 18: sign of result with high-bit bytes (0x80 vs 0x7f). */
static void sc_04_memcmp_high_bit_sign(void) {
    unsigned char a[2] = {0x80, 0x00};
    unsigned char b[2] = {0x7f, 0x00};
    int r = memcmp(a, b, 2);
    assert(r > 0);
    assert(sign_of(r) == sign_of(host_memcmp(a, b, 2)));
    assert(memcmp(b, a, 2) < 0);
}

/* 19: zero-length compare -> 0 for valid pointers.  A NULL operand is a
 * constraint violation even at n == 0 and is checked before the early return,
 * so both-NULL and NULL/valid return -1/EINVAL (RED-20 E1 contract). */
static void sc_04_memcmp_zero_len(void) {
    unsigned char a[2] = {0xff, 0x01};
    unsigned char b[2] = {0x00, 0x02};
    assert(memcmp(a, b, 0) == 0);
    errno = 0;
    assert(memcmp(NULL, NULL, 0) == -1 && errno == EINVAL);
    errno = 0;
    assert(memcmp(NULL, b, 0) == -1 && errno == EINVAL);
}

/* 20: NULL arguments with n > 0 return a concrete, documented result. */
static void sc_04_memcmp_null_args(void) {
    unsigned char b[4] = {1, 2, 3, 4};
    errno = 0;
    assert(memcmp(NULL, b, 4) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(memcmp(b, NULL, 4) == 1);
    assert(errno == EINVAL);
    errno = 0;
    assert(memcmp(NULL, NULL, 4) == -1 && errno == EINVAL);
}

/* 21: overlapping regions are legal for a read-only compare. */
static void sc_04_memcmp_overlapping(void) {
    unsigned char a[16];
    fill_seq(a, sizeof(a));
    assert(sign_of(memcmp(a, a + 1, 8)) == sign_of(host_memcmp(a, a + 1, 8)));
    assert(memcmp(a, a, 16) == 0);
    assert(memcmp(a + 3, a, 10) > 0);
}

/* 22: one-past-end bound: n == capacity is accepted, n == 0 at one-past-end
 *     is a defined early return. */
static void sc_04_memcmp_one_past_end(void) {
    unsigned char a[8], b[8];
    unsigned char *hp = (unsigned char *)malloc_s(8);
    unsigned char *hq = (unsigned char *)malloc_s(8);
    int r;
    assert(hp != NULL && hq != NULL);
    fill_seq(a, 8); fill_seq(b, 8);
    fill_seq(hp, 8); fill_seq(hq, 8);
    assert(memcmp(a, b, 8) == 0); /* exact capacity of both */
    r = cplus_memcmp_s_auto(a + 4, b + 4, 4,
                            CPLUS_SAFE_OBJ_SIZE(a + 4),
                            CPLUS_SAFE_OBJ_SIZE(b + 4));
    assert(sign_of(r) == sign_of(host_memcmp(a + 4, b + 4, 4)));
    assert(cplus_memcmp_s_auto(hp + 8, hq, 0,
                               cplus_safe_remaining_capacity(hp + 8),
                               cplus_safe_remaining_capacity(hq)) == 0);
    free_s(hp);
    free_s(hq);
}

/* 23: differently sized buffers compared with an explicit n. */
static void sc_04_memcmp_diff_sizes_explicit_n(void) {
    unsigned char big[16], small[4];
    fill_seq(big, sizeof(big));
    fill_seq(small, sizeof(small));
    assert(sign_of(memcmp(big, small, 4)) == sign_of(host_memcmp(big, small, 4)));
    assert(memcmp(big, small, 4) == 0); /* first four bytes are identical */
    small[2] = 0xee;
    assert(memcmp(big, small, 4) < 0);
}

/* 24: unsigned char semantics: 0xff/0x80 compare greater than 0x7f/0x01
 *     even though a signed char comparison would say the opposite. */
static void sc_04_memcmp_unsigned_semantics(void) {
    unsigned char a[3] = {0xff, 0x80, 0x01};
    unsigned char b[3] = {0x01, 0x7f, 0x01};
    assert(memcmp(a, b, 3) > 0);
    assert(sign_of(memcmp(a, b, 3)) == sign_of(host_memcmp(a, b, 3)));
    assert(memcmp(b, a, 3) < 0);
}

/* 25: strict unknown-capacity mode refuses both operations; default mode
 *     proceeds (with a warning) as raw C would.  The pointer comes from a
 *     volatile global load of a 16-byte stack buffer, so neither compile-time
 *     object size nor runtime allocation metadata can prove its capacity. */
static void sc_04_memcmp_strict_unknown_capacity(void) {
    unsigned char buf[16];
    unsigned char *p;
    fill_seq(buf, sizeof(buf));
    rmmc_opaque_store(buf);
    p = (unsigned char *)g_opaque_ptr;
    assert(CPLUS_SAFE_OBJ_SIZE(p) == (size_t)-1);
    assert(cplus_safe_remaining_capacity(p) == (size_t)-1);
    assert(cplus_safe_ptr_was_freed(p) == 0);

    cplus_safe_set_unknown_capacity_mode(1);
    errno = 0;
    assert(cplus_memmove_s_auto(p, p + 4, 8, (size_t)-1) == NULL);
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_memcmp_s_auto(p, p, 8, (size_t)-1, (size_t)-1) == -1);
    assert(errno == EINVAL);

    cplus_safe_set_unknown_capacity_mode(0);
    assert(cplus_memmove_s_auto(p, p + 4, 8, (size_t)-1) == p);
    assert(cplus_memcmp_s_auto(p, p, 8, (size_t)-1, (size_t)-1) == 0);
    assert(bytes_eq(buf, "\x05\x06\x07\x08\x09\x0a\x0b\x0c"
                         "\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10", 16));
    cplus_safe_set_unknown_capacity_mode(0);
}

/* 26: logging/telemetry parity: successes and refusals are counted once each
 *     under the API names the ordinary macros dispatch to. */
static void sc_04_memcmp_logging_parity(void) {
    cplus_safe_stat st;
    unsigned char a[8] = {0}, b[8] = {0};

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    assert(cplus_safe_log_get_level() == CPLUS_SAFE_LOG_OFF);
    cplus_safe_stats_reset();

    (void)memmove(a, b, 8); /* success */
    errno = 0;
    assert(cplus_memmove_s_auto(a, b, 9, sizeof(a)) == NULL); /* ESOVRLP */
    assert(errno == ERANGE);
    assert(memcmp(a, b, 8) == 0); /* success */
    assert(cplus_memcmp_s_auto(a, b, 9, sizeof(a), sizeof(b)) == -1);

    assert(cplus_safe_stats_get("memmove_s", &st) == 1);
    assert(st.calls == 2 && st.ok == 1 && st.fail == 1);
    assert(cplus_safe_stats_get("memcmp_s", &st) == 1);
    assert(st.calls == 2 && st.ok == 1 && st.fail == 1);

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_ERROR);
    assert(cplus_safe_log_get_level() == CPLUS_SAFE_LOG_ERROR);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
}

int main(void) {
    cplus_runtime_set_violation_trap(0); /* refuse-and-return, do not exit */
    assert(cplus_runtime_get_violation_trap() == 0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_unknown_capacity_mode(0);

    RUN(sc_04_memmove_overlap_left);
    RUN(sc_04_memmove_overlap_right);
    RUN(sc_04_memmove_adjacent);
    RUN(sc_04_memmove_self);
    RUN(sc_04_memmove_zero_len);
    RUN(sc_04_memmove_null_args);
    RUN(sc_04_memmove_interior_insufficient);
    RUN(sc_04_memmove_stack_dest);
    RUN(sc_04_memmove_global_dest);
    RUN(sc_04_memmove_struct_dest);
    RUN(sc_04_memmove_size_max);
    RUN(sc_04_memmove_uaf_refused);
    RUN(sc_04_memmove_realloc_stale_alias);
    RUN(sc_04_memmove_heap_exact_capacity);
    RUN(sc_04_memcmp_equal);
    RUN(sc_04_memcmp_first_byte_diff);
    RUN(sc_04_memcmp_last_byte_diff);
    RUN(sc_04_memcmp_high_bit_sign);
    RUN(sc_04_memcmp_zero_len);
    RUN(sc_04_memcmp_null_args);
    RUN(sc_04_memcmp_overlapping);
    RUN(sc_04_memcmp_one_past_end);
    RUN(sc_04_memcmp_diff_sizes_explicit_n);
    RUN(sc_04_memcmp_unsigned_semantics);
    RUN(sc_04_memcmp_strict_unknown_capacity);
    RUN(sc_04_memcmp_logging_parity);

    printf("runtime_memmove_memcmp: %d scenarios ok\n", g_scenarios);
    fflush(stdout);
    return 0;
}
