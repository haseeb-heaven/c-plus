/* Phase 5: string length + terminator and owned-key length + 1 arithmetic.
 *
 * Covered paths (guards run BEFORE any allocator call):
 *   string::string(text) .............. n < LIMIT before stdlib_alloc(n + 1)
 *   string::reserve(want) ............. want > LIMIT guard
 *   string::append(text) .............. extra > LIMIT-1 ||
 *                                       count > LIMIT-1-extra before
 *                                       need = count + extra + 1, and
 *                                       space*2 guarded before doubling
 *   strmap::stdlib_copy_key ........... len >= LIMIT before len + 1
 *   string::substr / field ............ n bounded by count <= LIMIT-1, so
 *                                       n + 1 cannot wrap size_t
 *
 * STDLIB_ALLOC_LIMIT = 4096 in source.  Self-checking: "<tag> <0|1>",
 * final "OVERFLOW_STRING <n> <f>".  Largest allocation is 4096 bytes.
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
#include "stdlib/string.hp"
#include "stdlib/collections.hp"

#define OVL_LIMIT ((size_t)4096)

static char ovl_big[OVL_LIMIT + 8];
static int ovl_checks = 0;
static int ovl_failed = 0;

static void ovl_chk(const char *tag, int cond) {
    ovl_checks++;
    if (!cond)
        ovl_failed++;
    print(tag, cond ? 1 : 0);
}

static int ovl_cb_calls;
static int ovl_cb_cat;
static cplus_exit_code ovl_cb_code;
static char ovl_cb_msg[128];

static void ovl_cb(int cat, cplus_exit_code code, const char *msg) {
    ovl_cb_calls++;
    ovl_cb_cat = cat;
    ovl_cb_code = code;
    if (msg != NULL) {
        size_t n = strlen(msg);
        if (n >= sizeof(ovl_cb_msg))
            n = sizeof(ovl_cb_msg) - 1;
        memcpy(ovl_cb_msg, msg, n);
        ovl_cb_msg[n] = '\0';
    }
}

static void ovl_cb_reset(void) {
    ovl_cb_calls = 0;
    ovl_cb_cat = -1;
    ovl_cb_code = CPLUS_EXIT_OK;
    ovl_cb_msg[0] = '\0';
}

/* Rejected reserve/append: NULL-effect on the string and zero allocation. */
static void ovl_reserve(void) {
    string ovl_s = string("");
    long seq_before;
    int ok;

    ovl_chk("ovl_ctor_empty", ovl_s.valid() == 1 && ovl_s.capacity() == 1 &&
                                  ovl_s.size() == 0);
    ovl_cb_reset();
    seq_before = stdlib_alloc_seq();
    ok = ovl_s.reserve(SIZE_MAX) == -1 && ovl_s.reserve(OVL_LIMIT + 1) == -1 &&
         stdlib_alloc_seq() == seq_before;
    ovl_chk("ovl_reserve_refused", ok);
    ovl_chk("ovl_reserve_category",
            ovl_cb_calls == 2 && ovl_cb_cat == STDLIB_CATEGORY_RANGE &&
                ovl_cb_code == CPLUS_EXIT_BOUNDS &&
                strcmp(ovl_cb_msg, "string::reserve: capacity too large") == 0);
    ovl_chk("ovl_reserve_state",
            ovl_s.size() == 0 && ovl_s.capacity() == 1 && ovl_s.valid() == 1 &&
                strcmp(ovl_s.c_str(), "") == 0);
    ovl_chk("ovl_reserve_limit", ovl_s.reserve(OVL_LIMIT) == 0 &&
                                     ovl_s.capacity() == OVL_LIMIT &&
                                     ovl_s.valid() == 1);
    ovl_s.drop();
}

static void ovl_append(void) {
    string ovl_s = string("");
    long seq_before;
    stdlib_telemetry_stats pre, post;
    int ok;

    memset(ovl_big, 'a', OVL_LIMIT);
    ovl_big[OVL_LIMIT - 2] = '\0'; /* strlen == 4094 == LIMIT-2 */

    ok = ovl_s.reserve(OVL_LIMIT - 1) == 0 && ovl_s.append(ovl_big) == 0;
    ovl_chk("ovl_append_4094", ok && ovl_s.size() == OVL_LIMIT - 2 &&
                                   ovl_s.capacity() == OVL_LIMIT - 1);

    ovl_cb_reset();
    stdlib_telemetry_snapshot(&pre);
    seq_before = stdlib_alloc_seq();
    ok = ovl_s.append("abcd") == -1; /* count 4094 + extra 4 + 1 > LIMIT */
    stdlib_telemetry_snapshot(&post);
    ovl_chk("ovl_append_refused", ok);
    ovl_chk("ovl_append_no_alloc",
            stdlib_alloc_seq() == seq_before &&
                post.alloc_count == pre.alloc_count &&
                post.api_failures == pre.api_failures + 1 &&
                ovl_cb_calls == 1 && ovl_cb_cat == STDLIB_CATEGORY_RANGE &&
                ovl_cb_code == CPLUS_EXIT_BOUNDS &&
                strcmp(ovl_cb_msg, "string::append: result too large") == 0);
    ovl_chk("ovl_append_state",
            ovl_s.size() == OVL_LIMIT - 2 && ovl_s.capacity() == OVL_LIMIT - 1 &&
                ovl_s.at(OVL_LIMIT - 3) == 'a' &&
                ovl_s.c_str()[OVL_LIMIT - 2] == '\0');

    /* Exact boundary: one byte fits (need == LIMIT), the next one does not. */
    ovl_cb_reset();
    ok = ovl_s.append("z") == 0 && ovl_s.size() == OVL_LIMIT - 1 &&
         ovl_s.capacity() == OVL_LIMIT;
    ovl_chk("ovl_append_exact_limit", ok);
    seq_before = stdlib_alloc_seq();
    ovl_cb_reset();
    ok = ovl_s.append("y") == -1 && stdlib_alloc_seq() == seq_before &&
         ovl_s.size() == OVL_LIMIT - 1 &&
         ovl_s.c_str()[OVL_LIMIT - 1] == '\0' && ovl_cb_calls == 1;
    ovl_chk("ovl_append_one_past_limit", ok);

    /* Self-append at the boundary is refused by the same subtraction guard. */
    seq_before = stdlib_alloc_seq();
    ok = ovl_s.append(ovl_s.c_str()) == -1 && stdlib_alloc_seq() == seq_before &&
         ovl_s.size() == OVL_LIMIT - 1;
    ovl_chk("ovl_append_self_refused", ok);
    ovl_s.drop();
}

/* strmap key copies: len + 1 is guarded by len >= LIMIT.  Prime the map
 * first so the put() itself needs no table allocation; then the rejected key
 * copy is the only allocation point and seq must not move. */
static void ovl_key_copy(void) {
    strmap ovl_sm = strmap();
    long seq_before;
    int ok;

    ok = ovl_sm.put("seed", 1) == 0 && ovl_sm.valid() == 1;
    ovl_chk("ovl_strmap_prime", ok);

    memset(ovl_big, 'k', OVL_LIMIT);
    ovl_big[OVL_LIMIT] = '\0'; /* strlen == 4096 == LIMIT -> must refuse */
    seq_before = stdlib_alloc_seq();
    ok = ovl_sm.put(ovl_big, 7) == -1 && stdlib_alloc_seq() == seq_before &&
         ovl_sm.size() == 1 && ovl_sm.contains("seed") == 1 &&
         ovl_sm.contains(ovl_big) == 0;
    ovl_chk("ovl_strmap_key_len_limit", ok);

    /* len == LIMIT-1 is accepted: len + 1 == LIMIT bytes. */
    ovl_big[OVL_LIMIT - 1] = '\0';
    ok = ovl_sm.put(ovl_big, 9) == 0 && ovl_sm.size() == 2 &&
         ovl_sm.contains(ovl_big) == 1;
    ovl_chk("ovl_strmap_key_len_limit_minus1", ok);
    ovl_sm.drop();
}

/* n + 1 in substr/field is safe because n <= count <= LIMIT-1; verify the
 * clamping boundary explicitly with small output buffers. */
static void ovl_substr(void) {
    string ovl_s2 = string("abcdef");
    char ovl_out[8];
    int ok;

    memset(ovl_out, 'X', sizeof(ovl_out));
    ok = ovl_s2.substr(0, 100, ovl_out, sizeof(ovl_out)) == 0 &&
         strcmp(ovl_out, "abcdef") == 0; /* clamped to 6 chars + NUL */
    ovl_chk("ovl_substr_clamp", ok);
    memset(ovl_out, 'X', sizeof(ovl_out));
    ok = ovl_s2.substr(0, 100, ovl_out, 1) == 0 && ovl_out[0] == '\0';
    ovl_chk("ovl_substr_size1", ok);
    ok = ovl_s2.substr(0, 100, ovl_out, 0) == -1;
    ovl_chk("ovl_substr_size0", ok);
    ok = ovl_s2.field('c', 0, ovl_out, sizeof(ovl_out)) == 0 &&
         strcmp(ovl_out, "ab") == 0;
    ovl_chk("ovl_field", ok);
    ovl_s2.drop();
}

int main(void) {
    stdlib_set_error_callback(ovl_cb);
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();
    ovl_cb_reset();
    ovl_reserve();
    ovl_append();
    ovl_key_copy();
    ovl_substr();
    stdlib_set_error_callback(NULL);
    print("OVERFLOW_STRING", ovl_checks);
    print("OVERFLOW_STRING_FAILURES", ovl_failed);
    return ovl_failed == 0 ? 0 : 1;
}
