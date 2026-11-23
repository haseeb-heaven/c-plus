/* Adversarial regression family: integer boundaries, overflow and allocation
 * guards (agent 5).
 *
 * Scope: INT_MIN/MAX, LONG_MIN/MAX, SIZE_MAX and UINT64_MAX are driven through
 * the checked allocator and the STDLIB checked containers.  STDLIB_ALLOC_LIMIT
 * is pinned to 4096 bytes IN SOURCE (before the includes) so that capacity
 * doubling, count*size overflow and the "too large" guards are reachable with
 * a handful of bytes instead of memory exhaustion.  The C+ strict bounds
 * checker does not consume -D flags, so the source-level define is required;
 * the build commands use the same value.  No header is edited.
 *
 * Every check prints "<tag> <0|1>" via the builtin print().  The last line is
 * "ADV_INTEGER_BOUNDS <checks> <failures>"; the exit code is 0 only when every
 * check passed.  All test locals use the aib_ prefix (C+ RAII lowering tracks
 * owned names per function).  The largest allocation in this program is
 * 4096 bytes; there is no fault injection and no memory exhaustion.
 */
#ifndef STDLIB_ALLOC_LIMIT
#define STDLIB_ALLOC_LIMIT ((size_t)4096)
#endif

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"
#include "stdlib/collections.hp"

#define AIB_LIMIT 4096
#define AIB_ELEMS 1024 /* AIB_LIMIT / sizeof(int) */

static int aib_checks = 0;
static int aib_failures = 0;
static char aib_big[AIB_LIMIT + 16];

static void aib_chk(const char *aib_tag, int aib_cond) {
    aib_checks++;
    if (!aib_cond)
        aib_failures++;
    print(aib_tag, aib_cond ? 1 : 0);
}

/* ------------------------------------------------------------------ *
 * 1. Boundary constants themselves (no arithmetic overflow in the test).
 * ------------------------------------------------------------------ */
static void aib_constants(void) {
    int aib_ok = 1;
    aib_ok = aib_ok && INT_MIN < 0 && INT_MAX > 0 &&
             ((long long)INT_MAX + 1LL) == 2147483648LL &&
             ((long long)INT_MIN - 1LL) == -2147483649LL;
    aib_chk("aib_int_const", aib_ok);

    aib_ok = LONG_MIN < 0L && LONG_MAX > 0L &&
             (LONG_MAX / 2L) >= (long)INT_MAX &&
             (LONG_MIN / 2L) <= (long)INT_MIN;
    aib_chk("aib_long_const", aib_ok);

    aib_ok = SIZE_MAX > (size_t)AIB_LIMIT &&
             (SIZE_MAX - (size_t)1) != SIZE_MAX &&
             (SIZE_MAX >> 1) == SIZE_MAX / (size_t)2 &&
             ((size_t)0 - (size_t)1) == SIZE_MAX;
    aib_chk("aib_size_const", aib_ok);

    aib_ok = UINT64_MAX == 0xffffffffffffffffULL &&
             (UINT64_MAX >> 32) == 0xffffffffULL &&
             (UINT64_MAX - 1ULL) == 0xfffffffffffffffeULL &&
             (SIZE_MAX >= UINT64_MAX ? (size_t)UINT64_MAX == SIZE_MAX : 1);
    aib_chk("aib_u64_const", aib_ok);

    aib_chk("aib_widths", sizeof(int) >= 4 && sizeof(long) >= 4 &&
                              sizeof(size_t) >= 4 && sizeof(void *) >= 4);
}

/* ------------------------------------------------------------------ *
 * 2. Allocator refusal paths: limit, SIZE_MAX/UINT64_MAX, count*size,
 *    and failure-atomic realloc.  Telemetry proves no hidden allocation.
 * ------------------------------------------------------------------ */
static void aib_allocator(void) {
    stdlib_telemetry_stats aib_pre;
    stdlib_telemetry_stats aib_post;
    void *aib_block = NULL;
    void *aib_zero = NULL;
    int aib_ok = 1;
    int aib_i = 0;

    for (aib_i = 0; aib_i < AIB_LIMIT; aib_i++)
        aib_big[aib_i] = 'a';
    aib_big[AIB_LIMIT] = '\0';

    stdlib_telemetry_snapshot(&aib_pre);
    aib_block = stdlib_alloc(AIB_LIMIT); /* the one allowed allocation */
    aib_ok = aib_ok && (aib_block != NULL);
    aib_ok = aib_ok && (stdlib_alloc(AIB_LIMIT + 1) == NULL);
    aib_ok = aib_ok && (stdlib_alloc(SIZE_MAX) == NULL);
    aib_ok = aib_ok && (stdlib_alloc((size_t)UINT64_MAX) == NULL);
    aib_ok = aib_ok && (stdlib_calloc(SIZE_MAX, (size_t)2) == NULL);
    aib_ok = aib_ok && (stdlib_calloc((size_t)2, SIZE_MAX) == NULL);
    aib_ok = aib_ok && (stdlib_calloc((SIZE_MAX / (size_t)2) + 1u, (size_t)2) == NULL);
    aib_ok = aib_ok && (stdlib_calloc((size_t)UINT64_MAX, (size_t)UINT64_MAX) == NULL);
    aib_ok = aib_ok && (stdlib_calloc((size_t)INT_MAX, (size_t)INT_MAX) == NULL);
    aib_ok = aib_ok && (stdlib_realloc(aib_block, SIZE_MAX) == NULL);
    stdlib_telemetry_snapshot(&aib_post);
    aib_chk("aib_alloc_refusals", aib_ok);
    aib_chk("aib_alloc_no_growth",
            aib_post.alloc_count == aib_pre.alloc_count + 1u &&
                aib_post.api_failures > aib_pre.api_failures &&
                aib_post.ownership_violations == aib_pre.ownership_violations);

    if (aib_block != NULL) {
        ((char *)aib_block)[0] = 'Q';
        aib_chk("aib_realloc_failure_atomic",
                stdlib_alloc_size(aib_block) == (size_t)AIB_LIMIT &&
                    ((char *)aib_block)[0] == 'Q');
        stdlib_release(aib_block);
    } else {
        aib_chk("aib_realloc_failure_atomic", 0);
    }

    /* count==0 is explicitly allowed even with an extreme element size. */
    aib_zero = stdlib_calloc((size_t)0, SIZE_MAX);
    aib_chk("aib_calloc_zero_count", aib_zero != NULL);
    if (aib_zero != NULL)
        stdlib_release(aib_zero);
}

/* ------------------------------------------------------------------ *
 * 3. Container size guards: EXTREME wants are refused without allocating
 *    and leave the container byte-for-byte usable.
 * ------------------------------------------------------------------ */
static void aib_container_refusals(void) {
    stdlib_telemetry_stats aib_pre;
    stdlib_telemetry_stats aib_post;
    vector aib_vec = vector();
    string aib_str = string("");
    intmap aib_im = intmap();
    strmap aib_sm = strmap();
    intset aib_is = intset();
    int aib_ok = 1;

    stdlib_telemetry_snapshot(&aib_pre);
    aib_ok = aib_ok && aib_vec.reserve(SIZE_MAX) == -1;
    aib_ok = aib_ok && aib_vec.reserve((size_t)UINT64_MAX) == -1;
    aib_ok = aib_ok && aib_str.reserve(SIZE_MAX) == -1;
    aib_ok = aib_ok && aib_str.reserve((size_t)UINT64_MAX) == -1;
    aib_ok = aib_ok && aib_str.append(aib_big) == -1; /* len 4096 > LIMIT-1 */
    aib_ok = aib_ok && aib_im.grow(SIZE_MAX) == -1;
    aib_ok = aib_ok && aib_im.grow((size_t)UINT64_MAX) == -1;
    aib_ok = aib_ok && aib_sm.grow(SIZE_MAX) == -1;
    aib_ok = aib_ok && aib_is.grow(SIZE_MAX) == -1;
    aib_ok = aib_ok && stdlib_map_next_pow2(SIZE_MAX) == 0;
    aib_ok = aib_ok && stdlib_map_next_pow2((size_t)UINT64_MAX) == 0;
    aib_ok = aib_ok &&
             stdlib_map_rebuild_capacity(SIZE_MAX - (size_t)1,
                                         SIZE_MAX - (size_t)1) == 0;
    stdlib_telemetry_snapshot(&aib_post);
    aib_chk("aib_container_refusals", aib_ok);
    aib_chk("aib_container_no_growth",
            aib_post.alloc_count == aib_pre.alloc_count &&
                aib_post.api_failures > aib_pre.api_failures);
    aib_chk("aib_container_unchanged",
            aib_vec.size() == 0 && aib_vec.capacity() == 0 &&
                aib_str.size() == 0 && aib_str.capacity() == 1 &&
                aib_str.valid() == 1 && strcmp(aib_str.c_str(), "") == 0 &&
                aib_im.size() == 0 && aib_im.valid() == 0 &&
                aib_sm.size() == 0 && aib_sm.valid() == 0 &&
                aib_is.size() == 0 && aib_is.valid() == 0);
}

/* ------------------------------------------------------------------ *
 * 4. INT_MIN / LONG_MIN cast to size_t are huge unsigned sizes and must
 *    be refused by every guard (never wrapped into a small allocation).
 * ------------------------------------------------------------------ */
static void aib_negative_casts(void) {
    stdlib_telemetry_stats aib_pre;
    stdlib_telemetry_stats aib_post;
    vector aib_vec = vector();
    string aib_str = string("");
    int aib_ok = 1;

    stdlib_telemetry_snapshot(&aib_pre);
    aib_ok = aib_ok && aib_vec.reserve((size_t)INT_MIN) == -1;
    aib_ok = aib_ok && aib_vec.reserve((size_t)LONG_MIN) == -1;
    aib_ok = aib_ok && aib_vec.reserve((size_t)INT_MAX) == -1;
    aib_ok = aib_ok && aib_str.reserve((size_t)INT_MIN) == -1;
    aib_ok = aib_ok && aib_str.reserve((size_t)LONG_MIN) == -1;
    aib_ok = aib_ok && aib_str.reserve((size_t)INT_MAX) == -1;
    aib_ok = aib_ok && stdlib_alloc((size_t)INT_MIN) == NULL;
    aib_ok = aib_ok && stdlib_alloc((size_t)LONG_MIN) == NULL;
    aib_ok = aib_ok && stdlib_calloc((size_t)INT_MIN, (size_t)4) == NULL;
    aib_ok = aib_ok && stdlib_calloc((size_t)LONG_MAX, (size_t)8) == NULL;
    stdlib_telemetry_snapshot(&aib_post);
    aib_chk("aib_negative_size_casts", aib_ok);
    aib_chk("aib_negative_size_no_growth",
            aib_post.alloc_count == aib_pre.alloc_count &&
                aib_post.api_failures > aib_pre.api_failures);
    aib_chk("aib_negative_size_no_wrap",
            aib_vec.size() == 0 && aib_vec.capacity() == 0 &&
                aib_str.size() == 0 && strcmp(aib_str.c_str(), "") == 0);
}

/* ------------------------------------------------------------------ *
 * 5. Real doubling guards: fill each container to exactly 1024 ints,
 *    then the next push must be refused with no allocation, and the
 *    container must still return the correct last element.
 * ------------------------------------------------------------------ */
static void aib_growth_guards(void) {
    stdlib_telemetry_stats aib_pre;
    stdlib_telemetry_stats aib_post;
    vector aib_vec = vector();
    deque aib_dq = deque();
    stack aib_stk = stack();
    queue aib_q = queue();
    int aib_all = 1;
    int aib_i = 0;
    int aib_v = 0;

    for (aib_i = 0; aib_i < AIB_ELEMS; aib_i++) {
        if (aib_vec.push_back(aib_i) != 0) aib_all = 0;
        if (aib_dq.push_back(aib_i) != 0) aib_all = 0;
        if (aib_stk.push(aib_i) != 0) aib_all = 0;
        if (aib_q.push(aib_i) != 0) aib_all = 0;
    }
    aib_chk("aib_growth_fill", aib_all && aib_vec.size() == AIB_ELEMS &&
                                   aib_dq.size() == AIB_ELEMS &&
                                   aib_stk.size() == AIB_ELEMS &&
                                   aib_q.size() == AIB_ELEMS &&
                                   aib_vec.capacity() == AIB_ELEMS &&
                                   aib_dq.capacity() == AIB_ELEMS);

    stdlib_telemetry_snapshot(&aib_pre);
    aib_chk("aib_vec_doubling_refused", aib_vec.push_back(-1) == -1);
    aib_chk("aib_deque_doubling_refused", aib_dq.push_back(-1) == -1);
    aib_chk("aib_stack_doubling_refused", aib_stk.push(-1) == -1);
    aib_chk("aib_queue_doubling_refused", aib_q.push(-1) == -1);
    stdlib_telemetry_snapshot(&aib_post);
    aib_chk("aib_growth_refusal_no_alloc",
            aib_post.alloc_count == aib_pre.alloc_count);

    aib_v = -99;
    aib_chk("aib_growth_still_readable",
            aib_vec.at_checked((size_t)(AIB_ELEMS - 1), &aib_v) == 0 &&
                aib_v == AIB_ELEMS - 1 &&
                aib_dq.at_checked((size_t)(AIB_ELEMS - 1), &aib_v) == 0 &&
                aib_v == AIB_ELEMS - 1 &&
                aib_stk.top_checked(&aib_v) == 0 &&
                aib_v == AIB_ELEMS - 1 &&
                aib_q.front_checked(&aib_v) == 0 && aib_v == 0 &&
                aib_vec.size() == AIB_ELEMS && aib_dq.size() == AIB_ELEMS &&
                aib_stk.size() == AIB_ELEMS && aib_q.size() == AIB_ELEMS);
}

/* ------------------------------------------------------------------ *
 * 6. String capacity doubling at the byte limit (4095 content bytes
 *    + NUL); one more byte must be refused without changing the string.
 * ------------------------------------------------------------------ */
static void aib_string_growth(void) {
    string aib_str = string("");
    stdlib_telemetry_stats aib_pre;
    stdlib_telemetry_stats aib_post;
    int aib_ok = 1;

    aib_ok = aib_ok && aib_str.reserve((size_t)AIB_LIMIT) == 0 &&
             aib_str.capacity() == (size_t)AIB_LIMIT;
    aib_big[3000] = '\0';
    aib_ok = aib_ok && aib_str.append(aib_big) == 0 && aib_str.size() == 3000;
    aib_big[3000] = 'a';
    aib_big[1095] = '\0';
    aib_ok = aib_ok && aib_str.append(aib_big) == 0 && aib_str.size() == 4095;
    aib_big[1095] = 'a';
    aib_chk("aib_string_growth_fill", aib_ok && aib_str.at(0) == 'a' &&
                                          aib_str.at(4094) == 'a');

    stdlib_telemetry_snapshot(&aib_pre);
    aib_ok = aib_str.append("x") == -1;
    aib_ok = aib_ok && aib_str.append(aib_big) == -1; /* 4096-byte token */
    stdlib_telemetry_snapshot(&aib_post);
    aib_chk("aib_string_doubling_refused", aib_ok);
    aib_chk("aib_string_no_growth",
            aib_post.alloc_count == aib_pre.alloc_count);
    aib_chk("aib_string_unchanged",
            aib_str.size() == 4095 && aib_str.capacity() == (size_t)AIB_LIMIT &&
                aib_str.at(0) == 'a' && aib_str.at(4094) == 'a' &&
                strlen(aib_str.c_str()) == 4095);
}

int main(void) {
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);

    aib_constants();
    aib_allocator();
    aib_container_refusals();
    aib_negative_casts();
    aib_growth_guards();
    aib_string_growth();

    print("ADV_INTEGER_BOUNDS", aib_checks, aib_failures);
    return aib_failures == 0 ? 0 : 1;
}
