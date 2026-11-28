/*
 * test_vendor_spn.c -- standalone C99 test for the vendored safeclib span
 * helpers in libc/src/safecrt_vendor/spn_group.c.
 *
 * Build/link (from the repo root):
 *   /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
 *     -Ithird_party/safeclib/include -Ibuild/safe \
 *     -Ithird_party/safeclib/src -Istdlib/safe \
 *     -Wno-macro-redefined -c libc/src/safecrt_vendor/spn_group.c \
 *     -o /tmp/spn_group.o
 *   /usr/bin/clang -O2 -std=c99 -Ilibc/install-include \
 *     tests/libc/test_vendor_spn.c /tmp/spn_group.o \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -o /tmp/test_vendor_spn
 *
 * Prints: vendor_spn: <checks> <failures>; exits nonzero on any failure.
 */
#include "cplus_vendor_spn.h"

#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define UNKNOWN CPLUS_V_SPAN_BOS_UNKNOWN

/* Valid span counts, with parity against ordinary strspn/strcspn. */
static void test_valid_counts(void)
{
    size_t n = 999;
    int rc;

    rc = cplus_v__strspn_s_chk("aabbccXYZ", strlen("aabbccXYZ"), "abc",
                               strlen("abc"), &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_EOK);
    CHECK(n == strspn("aabbccXYZ", "abc"));
    CHECK(n == 6);

    n = 999;
    rc = cplus_v__strspn_s_chk("xyzabc", strlen("xyzabc"), "abc",
                               strlen("abc"), &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_EOK);
    CHECK(n == strspn("xyzabc", "abc"));
    CHECK(n == 0);

    n = 999;
    rc = cplus_v__strspn_s_chk("abcabc", strlen("abcabc"), "abc",
                               strlen("abc"), &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_EOK);
    CHECK(n == strspn("abcabc", "abc"));
    CHECK(n == 6);

    /* dmax truncates the span even when more of dest matches. */
    n = 999;
    rc = cplus_v__strspn_s_chk("aaaaaa", 3, "a", 1, &n,
                               UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_EOK);
    CHECK(n == 3);

    /* slen truncates the search set. */
    n = 999;
    rc = cplus_v__strspn_s_chk("abcabc", strlen("abcabc"), "abcXYZ", 3, &n,
                               UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_EOK);
    CHECK(n == 6);

    n = 999;
    rc = cplus_v__strcspn_s_chk("abcXYZabc", strlen("abcXYZabc"), "XYZ",
                                strlen("XYZ"), &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_EOK);
    CHECK(n == strcspn("abcXYZabc", "XYZ"));
    CHECK(n == 3);

    n = 999;
    rc = cplus_v__strcspn_s_chk("XYZabc", strlen("XYZabc"), "XYZ",
                                strlen("XYZ"), &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_EOK);
    CHECK(n == strcspn("XYZabc", "XYZ"));
    CHECK(n == 0);

    n = 999;
    rc = cplus_v__strcspn_s_chk("no delimiters here",
                                strlen("no delimiters here"), "XYZ",
                                strlen("XYZ"), &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_EOK);
    CHECK(n == strcspn("no delimiters here", "XYZ"));
    CHECK(n == strlen("no delimiters here"));

    n = 999;
    rc = cplus_v__strcspn_s_chk("abcdef", strlen("abcdef"), "d", 1, &n,
                                UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_EOK);
    CHECK(n == strcspn("abcdef", "d"));
    CHECK(n == 3);
}

/* NULL pointer constraint violations (ESNULLP), checked before any access. */
static void test_null(void)
{
    size_t n = 999;
    int rc;

    rc = cplus_v__strspn_s_chk(NULL, 3, "abc", 3, &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESNULLP);

    rc = cplus_v__strspn_s_chk("abc", 3, NULL, 3, &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESNULLP);

    rc = cplus_v__strspn_s_chk("abc", 3, "abc", 3, NULL, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESNULLP);

    rc = cplus_v__strcspn_s_chk(NULL, 3, "abc", 3, &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESNULLP);

    rc = cplus_v__strcspn_s_chk("abc", 3, NULL, 3, &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESNULLP);

    rc = cplus_v__strcspn_s_chk("abc", 3, "abc", 3, NULL, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESNULLP);
}

/* Zero-length constraint violations (ESZEROL). */
static void test_zero_bounds(void)
{
    size_t n = 999;
    int rc;

    rc = cplus_v__strspn_s_chk("abc", 0, "abc", 3, &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESZEROL);

    rc = cplus_v__strspn_s_chk("abc", 3, "abc", 0, &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESZEROL);

    rc = cplus_v__strcspn_s_chk("abc", 0, "abc", 3, &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESZEROL);

    rc = cplus_v__strcspn_s_chk("abc", 3, "abc", 0, &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESZEROL);
}

/* One-past RSIZE_MAX_STR constraint violations (ESLEMAX). */
static void test_one_over_bounds(void)
{
    const size_t over = CPLUS_V_SPAN_RSIZE_MAX_STR + 1;
    size_t n = 999;
    int rc;

    rc = cplus_v__strspn_s_chk("abc", over, "abc", 3, &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESLEMAX);

    rc = cplus_v__strspn_s_chk("abc", 3, "abc", over, &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESLEMAX);

    rc = cplus_v__strcspn_s_chk("abc", over, "abc", 3, &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESLEMAX);

    rc = cplus_v__strcspn_s_chk("abc", 3, "abc", over, &n, UNKNOWN, UNKNOWN);
    CHECK(rc == CPLUS_V_SPAN_ESLEMAX);
}

int main(void)
{
    test_valid_counts();
    test_null();
    test_zero_bounds();
    test_one_over_bounds();

    printf("vendor_spn: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
