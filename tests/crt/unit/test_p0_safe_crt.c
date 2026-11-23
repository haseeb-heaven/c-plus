/*
 * test_p0_safe_crt.c
 *
 * P0 regression tests for the Safe CRT memory/string adapters
 * (stdlib/safe/safe_memory.h, safe_string.h, safe_format.h).
 *
 * Scope of this file:
 *   - memcpy_s / memmove_s overlap in BOTH directions, zero length, NULL,
 *     and size-limit violations (with guard bytes proving no write past dmax).
 *   - memcmp_s ordering/boundaries, cplus_strlen and cplus_strnlen boundaries.
 *   - strcpy_s / strncpy_s / strcat_s / strncat_s exact-fit, truncation,
 *     NULL arguments and refusal behaviour.
 *   - vsnprintf truncation via the va_list path (the existing format tests
 *     already cover the cplus_snprintf_s / cplus_sprintf_s truncation sweep;
 *     this file only adds the vsnprintf path plus one snprintf cross-check).
 *
 * The error codes are safeclib constraint codes (positive, NOT errno values).
 * On refusal safeclib puts the destination into a defined safe state: bytes
 * beyond dmax are never written, and the in-capacity prefix is cleared (mem)
 * or reset to the empty string (str). Tests assert those observable facts;
 * they do not assume "destination byte-for-byte unchanged".
 *
 * Output: one line `<tag> <0|1>` per check, then
 *   P0_SAFE_CRT <checks> <failures>
 * Exit status is 0 only when failures == 0.
 */
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "safe_memory.h"
#include "safe_string.h"
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"
#include "cplus_libc.h" /* cplus_strlen (hardened libc; both libs are linked) */

#ifndef ESNULLP_C
#define ESNULLP_C 400
#endif
#ifndef ESZEROL_C
#define ESZEROL_C 401
#endif
#ifndef ESLEMAX_C
#define ESLEMAX_C 403
#endif
#ifndef ESOVRLP_C
#define ESOVRLP_C 404
#endif
#ifndef ESNOSPC_C
#define ESNOSPC_C 406
#endif

static int g_checks = 0;
static int g_failures = 0;

static void ck(const char *tag, int ok) {
    ++g_checks;
    if (!ok) ++g_failures;
    printf("%s %d\n", tag, ok ? 1 : 0);
}

#define CHECK(tag, expr) ck((tag), (expr) ? 1 : 0)

static int all_bytes(const void *p, size_t n, unsigned char v) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; ++i)
        if (b[i] != v) return 0;
    return 1;
}

/* ---- memcpy_s / memmove_s: overlap, zero length, NULL, sizes ------------- */
static void test_memory_ops(void) {
    char a[16], b[16], c[16], m[16], n[16], d1[8], d2[8];
    struct { char dst[8]; char guard[8]; } g;

    memset(a, 0xCC, sizeof a);
    memcpy(a, "abcdefgh", 8);
    CHECK("memcpy_fwd_overlap_refused",
          cplus_memcpy_s(a + 1, 8, a, 8) == ESOVRLP_C);
    /* destination is a+1 for 8 bytes => guard starts at a+9 */
    CHECK("memcpy_fwd_overlap_guard", all_bytes(a + 9, 7, 0xCC));

    memset(b, 0xCC, sizeof b);
    memcpy(b, "abcdefgh", 8);
    CHECK("memcpy_bwd_overlap_refused",
          cplus_memcpy_s(b, 8, b + 1, 7) == ESOVRLP_C);
    CHECK("memcpy_bwd_overlap_guard", all_bytes(b + 8, 8, 0xCC));

    memset(c, 0xCC, sizeof c);
    memcpy(c, "abcdefgh", 8);
    CHECK("memcpy_same_ptr_ok", cplus_memcpy_s(c, 8, c, 8) == 0);
    CHECK("memcpy_same_ptr_data", memcmp(c, "abcdefgh", 8) == 0);

    memcpy(d2, "mondays", 8);
    CHECK("memcpy_distinct_ok",
          cplus_memcpy_s(d1, sizeof d1, d2, 8) == 0);
    CHECK("memcpy_distinct_data", memcmp(d1, "mondays", 8) == 0);

    memset(m, 0xCC, sizeof m);
    memcpy(m, "abcdefgh", 8);
    CHECK("memmove_fwd_overlap_ok",
          cplus_memmove_s(m + 1, 15, m, 8) == 0);
    CHECK("memmove_fwd_overlap_data", memcmp(m, "aabcdefgh", 9) == 0);
    CHECK("memmove_fwd_overlap_guard", all_bytes(m + 9, 7, 0xCC));

    memset(n, 0xCC, sizeof n);
    memcpy(n, "abcdefgh", 8);
    CHECK("memmove_bwd_overlap_ok",
          cplus_memmove_s(n, 15, n + 1, 4) == 0);
    CHECK("memmove_bwd_overlap_data", memcmp(n, "bcdeefgh", 8) == 0);
    CHECK("memmove_bwd_overlap_guard", all_bytes(n + 8, 8, 0xCC));

    CHECK("memmove_same_ptr_ok",
          cplus_memmove_s(m, sizeof m, m, 8) == 0);
    CHECK("memmove_same_ptr_data", memcmp(m, "aabcdefgh", 9) == 0);

    memset(m, 0xCC, sizeof m);
    CHECK("memcpy_smax_gt_dmax",
          cplus_memcpy_s(m, 4, m + 8, 8) == ESNOSPC_C);
    CHECK("memcpy_smax_gt_dmax_guard", all_bytes(m + 4, 12, 0xCC));
    CHECK("memmove_smax_gt_dmax",
          cplus_memmove_s(m, 4, m + 8, 8) == ESNOSPC_C);
    CHECK("memmove_smax_gt_dmax_guard", all_bytes(m + 4, 12, 0xCC));

    memset(g.dst, 'Q', sizeof g.dst);
    memset(g.guard, 0x5A, sizeof g.guard);
    CHECK("memcpy_null_src",
          cplus_memcpy_s(g.dst, sizeof g.dst, NULL, 4) == ESNULLP_C);
    CHECK("memcpy_null_src_guard",
          all_bytes(g.guard, sizeof g.guard, 0x5A));
    CHECK("memcpy_null_dest",
          cplus_memcpy_s(NULL, 8, "abcd", 4) == ESNULLP_C);
    CHECK("memmove_null_src",
          cplus_memmove_s(g.dst, sizeof g.dst, NULL, 4) == ESNULLP_C);
    CHECK("memmove_null_dest",
          cplus_memmove_s(NULL, 8, "abcd", 4) == ESNULLP_C);

    /* slen == 0 is defined: no pointer is dereferenced, nothing is written. */
    CHECK("memcpy_zero_len_null",
          cplus_memcpy_s(NULL, 8, NULL, 0) == 0);
    CHECK("memmove_zero_len_null",
          cplus_memmove_s(NULL, 8, NULL, 0) == 0);
    memset(g.dst, 'Q', sizeof g.dst);
    CHECK("memcpy_zero_len_no_write",
          cplus_memcpy_s(g.dst, sizeof g.dst, "abcd", 0) == 0);
    CHECK("memcpy_zero_len_data", all_bytes(g.dst, sizeof g.dst, 'Q'));

    CHECK("memcpy_dmax_zero",
          cplus_memcpy_s(g.dst, 0, "abcd", 4) == ESZEROL_C);
    CHECK("memmove_dmax_zero",
          cplus_memmove_s(g.dst, 0, "abcd", 4) == ESZEROL_C);
    CHECK("memcpy_dmax_over_max",
          cplus_memcpy_s(g.dst, (rsize_t)RSIZE_MAX_MEM + 1, "a", 1)
          == ESLEMAX_C);
    CHECK("memcpy_slen_over_max",
          cplus_memcpy_s(g.dst, 8, "abcdefgh", (rsize_t)RSIZE_MAX_MEM + 1)
          == ESLEMAX_C);
    CHECK("memmove_slen_over_max",
          cplus_memmove_s(g.dst, 8, "abcdefgh", (rsize_t)RSIZE_MAX_MEM + 1)
          == ESLEMAX_C);
}

/* ---- memcmp_s, strlen, strnlen boundaries -------------------------------- */
static void test_length_and_compare(void) {
    int diff;
    char nx[6] = { 'a', 'b', 'c', 'd', 'e', 'f' };
    char e[6] = { 'a', 'b', '\0', 'x', 'y', 'z' };
    struct { char buf[6]; char guard[8]; } g;
    size_t i;

    diff = 99;
    CHECK("memcmp_equal", cplus_memcmp_s("abc", 3, "abc", 3, &diff) == 0);
    CHECK("memcmp_equal_diff", diff == 0);
    diff = 99;
    CHECK("memcmp_less", cplus_memcmp_s("abc", 3, "abd", 3, &diff) == 0);
    CHECK("memcmp_less_diff", diff == -1);
    diff = 99;
    CHECK("memcmp_greater", cplus_memcmp_s("abe", 3, "abd", 3, &diff) == 0);
    CHECK("memcmp_greater_diff", diff == 1);
    diff = 99;
    CHECK("memcmp_prefix_only", cplus_memcmp_s("abZ", 3, "abC", 2, &diff) == 0);
    CHECK("memcmp_prefix_only_diff", diff == 0);
    diff = 99;
    CHECK("memcmp_unsigned_order",
          cplus_memcmp_s("\x00", 1, "\xFF", 1, &diff) == 0);
    CHECK("memcmp_unsigned_order_diff", diff == -1);
    diff = 77;
    CHECK("memcmp_null_diff",
          cplus_memcmp_s("abc", 3, "abc", 3, NULL) == ESNULLP_C);
    diff = 77;
    CHECK("memcmp_null_dest",
          cplus_memcmp_s(NULL, 3, "abc", 3, &diff) == ESNULLP_C);
    CHECK("memcmp_null_dest_diff_reset", diff == -1);
    diff = 77;
    CHECK("memcmp_null_src",
          cplus_memcmp_s("abc", 3, NULL, 3, &diff) == ESNULLP_C);
    CHECK("memcmp_null_src_diff_reset", diff == -1);
    diff = 77;
    CHECK("memcmp_zero_dmax",
          cplus_memcmp_s("abc", 0, "abc", 3, &diff) == ESZEROL_C);
    CHECK("memcmp_zero_dmax_diff_reset", diff == -1);
    diff = 77;
    CHECK("memcmp_smax_gt_dmax",
          cplus_memcmp_s("ab", 2, "abc", 3, &diff) == ESNOSPC_C);
    CHECK("memcmp_smax_gt_dmax_diff_reset", diff == -1);
    diff = 77;
    CHECK("memcmp_dmax_over_max",
          cplus_memcmp_s("abc", (rsize_t)RSIZE_MAX_MEM + 1, "abc", 3, &diff)
          == ESLEMAX_C);

    /* strlen (hardened libc): empty, exact, embedded NUL */
    CHECK("strlen_empty", cplus_strlen("") == 0);
    CHECK("strlen_plain", cplus_strlen("abc") == 3);
    CHECK("strlen_embedded_nul", cplus_strlen("ab\0cd") == 2);

    /* strnlen: bounded length never exceeds smax and stops at first NUL */
    CHECK("strnlen_within", cplus_strnlen_s("abc", 10) == 3);
    CHECK("strnlen_exact", cplus_strnlen_s("abc", 3) == 3);
    CHECK("strnlen_one_less", cplus_strnlen_s("abc", 2) == 2);
    CHECK("strnlen_one", cplus_strnlen_s("abc", 1) == 1);
    CHECK("strnlen_zero_smax", cplus_strnlen_s("abc", 0) == 0);
    CHECK("strnlen_empty", cplus_strnlen_s("", 5) == 0);
    CHECK("strnlen_null", cplus_strnlen_s(NULL, 10) == 0);
    CHECK("strnlen_over_max",
          cplus_strnlen_s("abc", (rsize_t)RSIZE_MAX_STR + 1) == 0);
    CHECK("strnlen_no_nul_in_window", cplus_strnlen_s(nx, 6) == 6);
    CHECK("strnlen_no_nul_short_window", cplus_strnlen_s(nx, 3) == 3);
    CHECK("strnlen_nul_in_window", cplus_strnlen_s(e, 6) == 2);
    CHECK("strnlen_agrees_strlen",
          cplus_strnlen_s("the quick brown fox",
                          sizeof "the quick brown fox") == strlen("the quick brown fox"));

    memcpy(g.buf, nx, sizeof g.buf);
    memset(g.guard, 0x5A, sizeof g.guard);
    CHECK("strnlen_read_only",
          cplus_strnlen_s(g.buf, sizeof g.buf) == sizeof g.buf);
    for (i = 0; i < sizeof g.guard; ++i) {
        char tag[32];
        snprintf(tag, sizeof tag, "strnlen_guard_%zu", i);
        CHECK(tag, g.guard[i] == 0x5A);
    }
}

/* ---- strcpy_s / strncpy_s / strcat_s / strncat_s ------------------------- */
static void test_string_ops(void) {
    char d[8], small[4];
    struct { char dst[8]; char guard[8]; } g;

    memset(d, 'Z', sizeof d);
    CHECK("strcpy_exact_fit", cplus_strcpy_s(d, 3, "ab") == 0);
    CHECK("strcpy_exact_fit_data", strcmp(d, "ab") == 0 && d[2] == '\0');
    CHECK("strcpy_empty", cplus_strcpy_s(d, sizeof d, "") == 0);
    CHECK("strcpy_empty_data", d[0] == '\0');

    memset(small, 'X', sizeof small);
    CHECK("strcpy_capacity_refused",
          cplus_strcpy_s(small, sizeof small, "abcdef") == ESNOSPC_C);
    /* refusal puts dst into a defined safe state; guard past dmax is intact */
    CHECK("strcpy_refusal_safe_state", small[0] == '\0');
    CHECK("strcpy_refusal_then_reuse",
          cplus_strcpy_s(small, sizeof small, "abc") == 0);
    CHECK("strcpy_refusal_reuse_data", strcmp(small, "abc") == 0);

    memset(g.dst, 'Q', sizeof g.dst);
    memset(g.guard, 0x5A, sizeof g.guard);
    CHECK("strcpy_refusal_no_overflow",
          cplus_strcpy_s(g.dst, 4, "0123456789") == ESNOSPC_C);
    CHECK("strcpy_refusal_guard", all_bytes(g.guard, sizeof g.guard, 0x5A));

    CHECK("strcpy_null_dest", cplus_strcpy_s(NULL, sizeof d, "hi") == ESNULLP_C);
    CHECK("strcpy_null_src", cplus_strcpy_s(d, sizeof d, NULL) == ESNULLP_C);
    CHECK("strcpy_dmax_zero", cplus_strcpy_s(d, 0, "hi") == ESZEROL_C);
    CHECK("strcpy_dmax_over_max",
          cplus_strcpy_s(d, (rsize_t)RSIZE_MAX_STR + 1, "hi") == ESLEMAX_C);
    strcpy(d, "hi");
    CHECK("strcpy_self_ok", cplus_strcpy_s(d, sizeof d, d) == 0);
    CHECK("strcpy_self_data", strcmp(d, "hi") == 0);

    /* strncpy: slen is the explicit copy count (truncation, not a refusal) */
    memset(d, 'Z', sizeof d);
    CHECK("strncpy_truncates", cplus_strncpy_s(d, sizeof d, "abcdef", 3) == 0);
    CHECK("strncpy_truncates_data", strcmp(d, "abc") == 0);
    memset(d, 'Z', sizeof d);
    CHECK("strncpy_slen_zero", cplus_strncpy_s(d, sizeof d, "abcdef", 0) == 0);
    CHECK("strncpy_slen_zero_data", d[0] == '\0');
    CHECK("strncpy_capacity_refused",
          cplus_strncpy_s(d, 4, "abcdef", 4) == ESNOSPC_C);
    CHECK("strncpy_refusal_safe_state", d[0] == '\0');
    CHECK("strncpy_null_dest",
          cplus_strncpy_s(NULL, sizeof d, "abc", 2) == ESNULLP_C);
    CHECK("strncpy_null_src",
          cplus_strncpy_s(d, sizeof d, NULL, 2) == ESNULLP_C);

    /* strcat: appends; exact fit is strlen(dst)+strlen(src)+1 == dmax */
    strcpy(d, "ab");
    CHECK("strcat_exact_fit", cplus_strcat_s(d, 5, "cd") == 0);
    CHECK("strcat_exact_fit_data", strcmp(d, "abcd") == 0);
    strcpy(d, "ab");
    CHECK("strcat_empty_src", cplus_strcat_s(d, 8, "") == 0);
    CHECK("strcat_empty_src_data", strcmp(d, "ab") == 0);
    strcpy(d, "ab");
    CHECK("strcat_capacity_refused",
          cplus_strcat_s(d, 4, "cdefgh") == ESNOSPC_C);
    CHECK("strcat_refusal_safe_state", d[0] == '\0');
    CHECK("strcat_null_dest", cplus_strcat_s(NULL, sizeof d, "x") == ESNULLP_C);
    CHECK("strcat_null_src", cplus_strcat_s(d, sizeof d, NULL) == ESNULLP_C);
    CHECK("strcat_dmax_zero", cplus_strcat_s(d, 0, "x") == ESZEROL_C);

    /* strncat: slen caps how many source chars are appended */
    strcpy(d, "ab");
    CHECK("strncat_truncates", cplus_strncat_s(d, sizeof d, "cdefgh", 3) == 0);
    CHECK("strncat_truncates_data", strcmp(d, "abcde") == 0);
    memset(d, 'Z', sizeof d);
    CHECK("strncat_slen_zero", cplus_strncat_s(d, sizeof d, "abc", 0) == ESZEROL_C);
    CHECK("strncat_slen_zero_safe_state", d[0] == '\0');
    strcpy(d, "abcdefg");
    CHECK("strncat_capacity_refused",
          cplus_strncat_s(d, 8, "x", 1) != 0);
    CHECK("strncat_refusal_safe_state", d[0] == '\0');
    CHECK("strncat_null_dest",
          cplus_strncat_s(NULL, sizeof d, "b", 1) == ESNULLP_C);
    CHECK("strncat_null_src",
          cplus_strncat_s(d, sizeof d, NULL, 1) == ESNULLP_C);

    /* guard bytes around a struct sub-object stay intact on every refusal.
     * Seed a terminated destination: an unterminated one is refused earlier
     * with ESUNTERM (407), which is a different constraint. */
    memset(g.dst, 'Q', sizeof g.dst);
    memset(g.guard, 0x5A, sizeof g.guard);
    strcpy(g.dst, "ab");
    CHECK("strncat_no_overflow",
          cplus_strncat_s(g.dst, 4, "0123456789", 4) == ESNOSPC_C);
    CHECK("strncat_no_overflow_guard", all_bytes(g.guard, sizeof g.guard, 0x5A));
}

/* helper: call cplus_vsnprintf_s with a variable argument list */
static int vfmt(char *buf, size_t cap, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vsnprintf_s(buf, cap, fmt, ap);
    va_end(ap);
    return r;
}

/* ---- vsnprintf truncation (va_list path) ------------------------------- */
static void test_vsnprintf(void) {
    char b[16];
    int sn, vn;

    memset(b, 'G', sizeof b);
    CHECK("vsn_fit", vfmt(b, sizeof b, "%s", "hi") == 2);
    CHECK("vsn_fit_data", strcmp(b, "hi") == 0);

    memset(b, 'G', sizeof b);
    CHECK("vsn_trunc_would_be_len", vfmt(b, 4, "%s", "abcdefgh") == 8);
    CHECK("vsn_trunc_nul", b[3] == '\0');
    CHECK("vsn_trunc_len", strlen(b) == 3);
    CHECK("vsn_trunc_guard", b[4] == 'G');

    memset(b, 'G', sizeof b);
    CHECK("vsn_dmax1", vfmt(b, 1, "%s", "abc") == 3);
    CHECK("vsn_dmax1_nul", b[0] == '\0' && b[1] == 'G');

    memset(b, 'G', sizeof b);
    CHECK("vsn_dmax0_query", vfmt(b, 0, "%s", "abc") == 3);
    CHECK("vsn_dmax0_no_write", b[0] == 'G');

    CHECK("vsn_null_query", vfmt(NULL, 0, "%s", "abc") == 3);
    CHECK("vsn_null_refusal", vfmt(NULL, 4, "%s", "abc") == -1);

    memset(b, 'G', sizeof b);
    CHECK("vsn_numeric_trunc", vfmt(b, 4, "%d", 1234567) == 7);
    CHECK("vsn_numeric_trunc_len", strlen(b) == 3);
    CHECK("vsn_numeric_trunc_guard", b[4] == 'G');

    /* cross-check: vsnprintf and snprintf must agree on the same truncation */
    memset(b, 'G', sizeof b);
    vn = vfmt(b, 5, "%s", "abcdefgh");
    sn = cplus_snprintf_s(b, 5, "%s", "abcdefgh");
    CHECK("vsn_matches_snprintf_len", vn == 8 && sn == 8);
    CHECK("vsn_matches_snprintf_data", memcmp(b, "abcd", 4) == 0 && b[4] == '\0');
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    test_memory_ops();
    test_length_and_compare();
    test_string_ops();
    test_vsnprintf();

    printf("P0_SAFE_CRT %d %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
