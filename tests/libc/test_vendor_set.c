/*
 * test_vendor_set.c -- standalone C99 test for the vendored safeclib string-set
 * helpers in libc/src/safecrt_vendor/set_group.c.
 *
 * Build/link (from the repo root):
 *   sh scripts/build_safecrt.sh >/dev/null 2>&1
 *   /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
 *     -Ithird_party/safeclib/include -Ibuild/safe \
 *     -Ithird_party/safeclib/src -Istdlib/safe \
 *     -Wno-macro-redefined -c libc/src/safecrt_vendor/set_group.c \
 *     -o /tmp/set_group.o
 *   /usr/bin/clang -O2 -std=c99 -Ilibc/install-include \
 *     tests/libc/test_vendor_set.c /tmp/set_group.o \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -o /tmp/test_vendor_set
 *
 * Prints: vendor_set: <checks> <failures>; exits nonzero on any failure.
 */
#include "cplus_vendor_set.h"

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

#define UNKNOWN CPLUS_V_SET_BOS_UNKNOWN

/* Install a silent constraint handler from the pinned safecrt archive so the
 * intentional violation cases below do not emit handler chatter. The handler
 * itself is upstream's set_str_constraint_handler_s, prefixed by the build. */
typedef void (*cplus_v_set_handler_t)(const char *, void *, int);
extern cplus_v_set_handler_t
cplus_sc_set_str_constraint_handler_s(cplus_v_set_handler_t handler);

static void silent_handler(const char *msg, void *ptr, int error)
{
    (void)msg;
    (void)ptr;
    (void)error;
}

static void test_fill_semantics(void)
{
    char a[8] = "abcdefg";
    int rc = cplus_v__strset_s_chk(a, sizeof(a), 'x', UNKNOWN);
    CHECK(rc == CPLUS_V_SET_EOK);
    CHECK(strcmp(a, "xxxxxxx") == 0);

    /* dmax caps the fill; bytes past dmax are untouched. */
    char b[8] = "abcdefg";
    rc = cplus_v__strset_s_chk(b, 4, 'x', UNKNOWN);
    CHECK(rc == CPLUS_V_SET_EOK);
    CHECK(strcmp(b, "xxxxefg") == 0);

    char c[8] = "abcdefg";
    rc = cplus_v__strnset_s_chk(c, sizeof(c), 'y', 3, UNKNOWN);
    CHECK(rc == CPLUS_V_SET_EOK);
    CHECK(strcmp(c, "yyydefg") == 0);

    /* n == 0 is a clean no-op. */
    char d[8] = "abcdefg";
    rc = cplus_v__strnset_s_chk(d, sizeof(d), 'y', 0, UNKNOWN);
    CHECK(rc == CPLUS_V_SET_EOK);
    CHECK(strcmp(d, "abcdefg") == 0);

    /* Fill stops at an embedded NUL before n is exhausted. */
    char e[8] = {'a', 'b', '\0', 'd', 'e', 'f', 'g', '\0'};
    rc = cplus_v__strnset_s_chk(e, sizeof(e), 'z', 5, UNKNOWN);
    CHECK(rc == CPLUS_V_SET_EOK);
    CHECK(strcmp(e, "zz") == 0);

    char f[8] = {'a', 'b', '\0', 'd', 'e', 'f', 'g', '\0'};
    rc = cplus_v__strset_s_chk(f, sizeof(f), 'z', UNKNOWN);
    CHECK(rc == CPLUS_V_SET_EOK);
    CHECK(strcmp(f, "zz") == 0);
}

static void test_exact_fit(void)
{
    /* dmax == strlen+1: exact fit, full fill, EOK. */
    char a[8] = "abcdefg";
    CHECK(cplus_v__strset_s_chk(a, 8, 'q', UNKNOWN) == CPLUS_V_SET_EOK);
    CHECK(strcmp(a, "qqqqqqq") == 0);

    /* n == dmax: EOK, fills every non-NUL byte without writing past it. */
    char b[8] = "abcdefg";
    CHECK(cplus_v__strnset_s_chk(b, 8, 'w', 8, UNKNOWN) == CPLUS_V_SET_EOK);
    CHECK(strcmp(b, "wwwwwww") == 0);

    /* Known destbos equal to dmax also takes the clean path. */
    char c[8] = "abcdefg";
    CHECK(cplus_v__strset_s_chk(c, sizeof(c), 'k', sizeof(c)) ==
          CPLUS_V_SET_EOK);
    CHECK(strcmp(c, "kkkkkkk") == 0);

    /* Known destbos equal to dmax for the terminating helper. */
    char d[8] = "abcdefg";
    CHECK(cplus_v__strnterminate_s_chk(d, sizeof(d), sizeof(d)) == 7);
    CHECK(strcmp(d, "abcdefg") == 0);
}

static void test_one_over(void)
{
    char a[8] = "abcdefg";

    /* n > dmax is the one-over case for strnset_s. */
    CHECK(cplus_v__strnset_s_chk(a, sizeof(a), 'x', sizeof(a) + 1, UNKNOWN) ==
          CPLUS_V_SET_ESNOSPC);
    CHECK(strcmp(a, "abcdefg") == 0);

    /* dmax > RSIZE_MAX_STR is ESLEMAX at the BOS_UNKNOWN path. */
    size_t over = CPLUS_V_SET_RSIZE_MAX_STR + 1;
    CHECK(cplus_v__strset_s_chk(a, over, 'x', UNKNOWN) ==
          CPLUS_V_SET_ESLEMAX);
    CHECK(cplus_v__strnset_s_chk(a, over, 'x', 1, UNKNOWN) ==
          CPLUS_V_SET_ESLEMAX);
    CHECK(cplus_v__strnterminate_s_chk(a, over, UNKNOWN) == 0);

    /* value > 255 is ESLEMAX for both setters. */
    CHECK(cplus_v__strset_s_chk(a, sizeof(a), 256, UNKNOWN) ==
          CPLUS_V_SET_ESLEMAX);
    CHECK(cplus_v__strnset_s_chk(a, sizeof(a), 256, 1, UNKNOWN) ==
          CPLUS_V_SET_ESLEMAX);

    /* A known destbos one byte smaller than dmax is EOVERFLOW. */
    CHECK(cplus_v__strset_s_chk(a, sizeof(a) + 1, 'x', sizeof(a)) ==
          CPLUS_V_SET_EOVERFLOW);
    CHECK(cplus_v__strnterminate_s_chk(a, sizeof(a) + 1, sizeof(a)) == 0);
    CHECK(strcmp(a, "abcdefg") == 0);
}

static void test_null(void)
{
    CHECK(cplus_v__strset_s_chk(NULL, 8, 'x', UNKNOWN) ==
          CPLUS_V_SET_ESNULLP);
    CHECK(cplus_v__strnset_s_chk(NULL, 8, 'x', 1, UNKNOWN) ==
          CPLUS_V_SET_ESNULLP);
    CHECK(cplus_v__strnterminate_s_chk(NULL, 8, UNKNOWN) == 0);

    char a[8] = "abcdefg";
    CHECK(cplus_v__strset_s_chk(a, 0, 'x', UNKNOWN) == CPLUS_V_SET_ESZEROL);
    CHECK(cplus_v__strnset_s_chk(a, 0, 'x', 0, UNKNOWN) ==
          CPLUS_V_SET_ESZEROL);
    CHECK(cplus_v__strnterminate_s_chk(a, 0, UNKNOWN) == 0);
    CHECK(strcmp(a, "abcdefg") == 0);
}

static void test_termination(void)
{
    /* No NUL within dmax: upstream writes the terminator at dmax-1. */
    char a[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
    CHECK(cplus_v__strnterminate_s_chk(a, sizeof(a), UNKNOWN) == 7);
    CHECK(a[7] == '\0');
    CHECK(strcmp(a, "ABCDEFG") == 0);

    /* dmax == strlen (no NUL in the counted span): terminate at dmax-1. */
    char b[8] = "abcdefg";
    CHECK(cplus_v__strnterminate_s_chk(b, 7, UNKNOWN) == 6);
    CHECK(strcmp(b, "abcdef") == 0);

    /* Early NUL: stop there and report the leading non-NUL count. */
    char c[8] = {'a', 'b', 'c', '\0', 'e', 'f', 'g', 'h'};
    CHECK(cplus_v__strnterminate_s_chk(c, sizeof(c), UNKNOWN) == 3);
    CHECK(strcmp(c, "abc") == 0);

    /* Small dmax boundaries. */
    char d[4] = {'a', 'b', 'c', 'd'};
    CHECK(cplus_v__strnterminate_s_chk(d, 2, UNKNOWN) == 1);
    CHECK(d[0] == 'a' && d[1] == '\0');
    char e[4] = {'a', 'b', 'c', 'd'};
    CHECK(cplus_v__strnterminate_s_chk(e, 1, UNKNOWN) == 0);
    CHECK(e[0] == '\0');
}

int main(void)
{
    cplus_sc_set_str_constraint_handler_s(silent_handler);

    test_fill_semantics();
    test_exact_fit();
    test_one_over();
    test_null();
    test_termination();

    printf("vendor_set: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
