/*
 * test_vendor_case.c -- standalone C99 test for the verbatim-vendored
 * safeclib case-conversion entry points in libc/src/safecrt_vendor/case_group.c.
 *
 * Compile/link from the repo root (after `sh scripts/build_safecrt.sh` has
 * generated build/safe/config.h and the safecrt archive):
 *
 *   /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
 *     -Ithird_party/safeclib/include -Ibuild/safe -Ithird_party/safeclib/src \
 *     -Istdlib/safe -Wno-macro-redefined -c \
 *     libc/src/safecrt_vendor/case_group.c -o /tmp/case_group.o
 *
 *   /usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
 *     -Ilibc/install-include \
 *     tests/libc/test_vendor_case.c \
 *     /tmp/case_group.o build/safe/libcplus_safecrt.a \
 *     build/libc/libcplus_hardened.a -o /tmp/test_vendor_case
 *
 * Prints exactly `vendor_case: <checks> <failures>` and exits nonzero if any
 * check failed.
 */
#include "cplus_vendor_case.h"

#include <errno.h>   /* EOVERFLOW */
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void test_lowercase_conversion(void)
{
    char buf[32];
    int rc;

    strcpy(buf, "AbC-XYz 09_Mn");
    rc = cplus_v__strtolowercase_s_chk(buf, strlen(buf), sizeof(buf));
    CHECK(rc == CPLUS_VENDOR_CASE_EOK);
    CHECK(strcmp(buf, "abc-xyz 09_mn") == 0);
    CHECK(buf[0] == 'a' && buf[3] == '-' && buf[10] == '_');
}

static void test_uppercase_conversion(void)
{
    char buf[32];
    int rc;

    strcpy(buf, "aBc-dEf 12_gh");
    rc = cplus_v__strtouppercase_s_chk(buf, strlen(buf), sizeof(buf));
    CHECK(rc == CPLUS_VENDOR_CASE_EOK);
    CHECK(strcmp(buf, "ABC-DEF 12_GH") == 0);
    CHECK(buf[0] == 'A' && buf[3] == '-' && buf[10] == '_');
}

static void test_stop_at_nul(void)
{
    char buf[8] = {'A', 'B', '\0', 'C', 'D', '\0', 0, 0};
    int rc;

    /* dmax spans the embedded NUL; conversion must stop there. */
    rc = cplus_v__strtolowercase_s_chk(buf, 5, sizeof(buf));
    CHECK(rc == CPLUS_VENDOR_CASE_EOK);
    CHECK(buf[0] == 'a' && buf[1] == 'b');
    CHECK(buf[2] == '\0' && buf[3] == 'C' && buf[4] == 'D');
}

static void test_stop_at_dmax(void)
{
    char buf[8] = {'A', 'B', 'C', 'D', 'E', '\0', 0, 0};
    int rc;

    /* No NUL inside the first 3 bytes: only those bytes may change. */
    rc = cplus_v__strtouppercase_s_chk(buf, 3, sizeof(buf));
    CHECK(rc == CPLUS_VENDOR_CASE_EOK);
    CHECK(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
    CHECK(buf[3] == 'D' && buf[4] == 'E' && buf[5] == '\0');
}

static void test_non_alpha_passthrough(void)
{
    static const char original[] = "0123 !@#_+-. [ ]";
    char lower[32];
    char upper[32];
    int rc;

    strcpy(lower, original);
    strcpy(upper, original);
    rc = cplus_v__strtolowercase_s_chk(lower, strlen(lower), sizeof(lower));
    CHECK(rc == CPLUS_VENDOR_CASE_EOK);
    CHECK(memcmp(lower, original, sizeof(original)) == 0);

    rc = cplus_v__strtouppercase_s_chk(upper, strlen(upper), sizeof(upper));
    CHECK(rc == CPLUS_VENDOR_CASE_EOK);
    CHECK(memcmp(upper, original, sizeof(original)) == 0);
}

static void test_null_dest(void)
{
    CHECK(cplus_v__strtolowercase_s_chk(NULL, 4,
                                        CPLUS_VENDOR_CASE_BOS_UNKNOWN)
          == CPLUS_VENDOR_CASE_ESNULLP);
    CHECK(cplus_v__strtouppercase_s_chk(NULL, 4,
                                        CPLUS_VENDOR_CASE_BOS_UNKNOWN)
          == CPLUS_VENDOR_CASE_ESNULLP);
}

static void test_zero_dmax(void)
{
    char buf[8];
    size_t bos = sizeof(buf);

    strcpy(buf, "abc");
    CHECK(cplus_v__strtolowercase_s_chk(buf, 0, bos)
          == CPLUS_VENDOR_CASE_ESZEROL);
    CHECK(strcmp(buf, "abc") == 0);

    CHECK(cplus_v__strtouppercase_s_chk(buf, 0, bos)
          == CPLUS_VENDOR_CASE_ESZEROL);
    CHECK(strcmp(buf, "abc") == 0);
}

static void test_one_over_max(void)
{
    char buf[8];
    size_t one_over_max = CPLUS_VENDOR_CASE_RSIZE_MAX_STR + 1;

    strcpy(buf, "abc");
    /* BOS_UNKNOWN path: CHK_DMAX_MAX catches dmax > RSIZE_MAX_STR. */
    CHECK(cplus_v__strtolowercase_s_chk(buf, one_over_max,
                                        CPLUS_VENDOR_CASE_BOS_UNKNOWN)
          == CPLUS_VENDOR_CASE_ESLEMAX);
    CHECK(cplus_v__strtouppercase_s_chk(buf, one_over_max,
                                        CPLUS_VENDOR_CASE_BOS_UNKNOWN)
          == CPLUS_VENDOR_CASE_ESLEMAX);
    CHECK(strcmp(buf, "abc") == 0);
}

static void test_one_over_dest(void)
{
    char buf[8];
    size_t bos = sizeof(buf);

    strcpy(buf, "abc");
    /* Known object size: dmax == destbos + 1 must be rejected before any
     * character is touched. */
    CHECK(cplus_v__strtolowercase_s_chk(buf, bos + 1, bos) == EOVERFLOW);
    CHECK(cplus_v__strtouppercase_s_chk(buf, bos + 1, bos) == EOVERFLOW);
    CHECK(strcmp(buf, "abc") == 0);
}

int main(void)
{
    test_lowercase_conversion();
    test_uppercase_conversion();
    test_stop_at_nul();
    test_stop_at_dmax();
    test_non_alpha_passthrough();
    test_null_dest();
    test_zero_dmax();
    test_one_over_max();
    test_one_over_dest();

    printf("vendor_case: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
