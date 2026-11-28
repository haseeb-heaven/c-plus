/*
 * test_vendor_field.c -- standalone C99 test for the vendored safeclib field
 * compare/copy and collation entry points in
 * libc/src/safecrt_vendor/field_group.c.
 *
 * The three functions under test are the pinned upstream rurban/safeclib
 * implementations (strcmpfld_s.c, strcpyfld_s.c, strcoll_s.c) included
 * verbatim with only symbol-prefix renames. This test links directly against
 * the freshly compiled object plus the checked runtime's static libraries:
 *
 *   /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
 *     tests/libc/test_vendor_field.c \
 *     /tmp/field_group.o build/safe/libcplus_safecrt.a \
 *     build/libc/libcplus_hardened.a -o /tmp/test_vendor_field
 */
#include "cplus_vendor_field.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define BOS CPLUS_V_FIELD_BOS_UNKNOWN
#define EOK CPLUS_V_FIELD_EOK
#define ESNULLP CPLUS_V_FIELD_ESNULLP
#define ESZEROL CPLUS_V_FIELD_ESZEROL
#define ESLEMAX CPLUS_V_FIELD_ESLEMAX
#define ESNOSPC CPLUS_V_FIELD_ESNOSPC
#define RSIZE_MAX_STR CPLUS_V_FIELD_RSIZE_MAX_STR

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

static void test_strcmpfld(void)
{
    const char eq[] = "abcdef";
    const char ne[] = "abcXef";
    const char with_nul_a[4] = {'a', 'b', '\0', 'x'};
    const char with_nul_b[4] = {'a', 'b', '\0', 'y'};
    char dest8[8] = "abcdef";
    int r = -99;
    int rc;

    /* Valid inputs: exact compare, sign, NUL ignored by field semantics,
     * and truncation at dmax. */
    rc = cplus_v__strcmpfld_s_chk(eq, 6, eq, &r, BOS);
    CHECK(rc == EOK);
    CHECK(r == 0);

    rc = cplus_v__strcmpfld_s_chk(eq, 6, ne, &r, BOS);
    CHECK(rc == EOK);
    CHECK(r > 0);
    CHECK(r == 'd' - 'X');

    rc = cplus_v__strcmpfld_s_chk(ne, 6, eq, &r, BOS);
    CHECK(rc == EOK);
    CHECK(r < 0);

    /* A NUL inside the field does NOT terminate the comparison. */
    rc = cplus_v__strcmpfld_s_chk(with_nul_a, 4, with_nul_b, &r, 4);
    CHECK(rc == EOK);
    CHECK(r == 'x' - 'y');

    /* Difference past dmax is ignored. */
    rc = cplus_v__strcmpfld_s_chk(eq, 3, ne, &r, BOS);
    CHECK(rc == EOK);
    CHECK(r == 0);

    /* Known destination size, dmax < destbos: no error. */
    rc = cplus_v__strcmpfld_s_chk(dest8, 4, dest8, &r, 8);
    CHECK(rc == EOK);
    CHECK(r == 0);

    /* NULL inputs. resultp is cleared before the dest/src checks. */
    r = 123;
    CHECK(cplus_v__strcmpfld_s_chk(NULL, 6, eq, &r, BOS) == ESNULLP);
    CHECK(r == 0);
    r = 123;
    CHECK(cplus_v__strcmpfld_s_chk(eq, 6, NULL, &r, BOS) == ESNULLP);
    CHECK(r == 0);
    CHECK(cplus_v__strcmpfld_s_chk(eq, 6, eq, NULL, BOS) == ESNULLP);

    /* Zero and one-over bounds. */
    r = 123;
    CHECK(cplus_v__strcmpfld_s_chk(eq, 0, eq, &r, BOS) == ESZEROL);
    CHECK(r == 0);
    r = 123;
    CHECK(cplus_v__strcmpfld_s_chk(eq, RSIZE_MAX_STR + 1, eq, &r, BOS) ==
          ESLEMAX);
    CHECK(r == 0);
    r = 123;
    CHECK(cplus_v__strcmpfld_s_chk(dest8, 7, dest8, &r, 6) == EOVERFLOW);
    CHECK(r == 0);
}

static void test_strcpyfld(void)
{
    const char embedded_nul[4] = {'A', '\0', 'B', 'C'};
    const char expected[6] = {'A', '\0', 'B', 'C', '\0', '\0'};
    const char expected_slack[8] = {'H', 'E', 'L', 'L', 'O', '\0', '\0', '\0'};
    char dest[8];
    char dest6[6];
    char keep[4] = {'k', 'e', 'e', 'p'};
    const char keep_copy[4] = {'k', 'e', 'e', 'p'};

    /* Valid copy: slen bytes copied, all remaining slack zeroed. */
    memset(dest, 'X', sizeof dest);
    CHECK(cplus_v__strcpyfld_s_chk(dest, 8, "HELLO", 5, 8) == EOK);
    CHECK(memcmp(dest, expected_slack, sizeof expected_slack) == 0);

    /* A NUL inside the source does not stop the copy. */
    memset(dest6, 'Z', sizeof dest6);
    CHECK(cplus_v__strcpyfld_s_chk(dest6, 6, embedded_nul, 4, 6) == EOK);
    CHECK(memcmp(dest6, expected, sizeof expected) == 0);

    /* Known dest size, dmax < destbos: valid. */
    memset(dest, 'X', sizeof dest);
    CHECK(cplus_v__strcpyfld_s_chk(dest, 6, "AB", 2, 8) == EOK);
    CHECK(dest[0] == 'A' && dest[1] == 'B');
    CHECK(dest[2] == '\0' && dest[5] == '\0');

    /* slen == 0 is EOK and performs no write (upstream early return). */
    CHECK(cplus_v__strcpyfld_s_chk(keep, 4, "AB", 0, 4) == EOK);
    CHECK(memcmp(keep, keep_copy, sizeof keep) == 0);
    CHECK(cplus_v__strcpyfld_s_chk(NULL, 4, "AB", 0, 4) == EOK);

    /* NULL inputs. */
    CHECK(cplus_v__strcpyfld_s_chk(NULL, 4, "AB", 2, 4) == ESNULLP);
    CHECK(cplus_v__strcpyfld_s_chk(dest, 4, NULL, 2, 4) == ESNULLP);

    /* Zero, oversized slen, max and one-over bounds. */
    CHECK(cplus_v__strcpyfld_s_chk(dest, 0, "AB", 2, 0) == ESZEROL);
    CHECK(cplus_v__strcpyfld_s_chk(dest, 4, "ABCDE", 5, 4) == ESNOSPC);
    CHECK(cplus_v__strcpyfld_s_chk(dest, 8, "AB", RSIZE_MAX_STR + 1, BOS) ==
          ESLEMAX);
    CHECK(cplus_v__strcpyfld_s_chk(dest, 5, "AB", 2, 4) == EOVERFLOW);
}

static void test_strcoll(void)
{
    const char abc[] = "abc";
    const char abd[] = "abd";
    char dest8[8] = "abc";
    int r = -99;
    int rc;

    /* Valid locale-aware comparisons (default "C" locale => sign of strcoll). */
    rc = cplus_v__strcoll_s_chk(abc, 3, abc, &r, BOS);
    CHECK(rc == EOK);
    CHECK(r == 0);
    rc = cplus_v__strcoll_s_chk(abc, 3, abd, &r, BOS);
    CHECK(rc == EOK);
    CHECK(r < 0);
    rc = cplus_v__strcoll_s_chk(abd, 3, abc, &r, BOS);
    CHECK(rc == EOK);
    CHECK(r > 0);
    rc = cplus_v__strcoll_s_chk(dest8, 3, dest8, &r, 8);
    CHECK(rc == EOK);
    CHECK(r == 0);

    /* NULL inputs; resultp cleared before dest/src checks. */
    r = 123;
    CHECK(cplus_v__strcoll_s_chk(NULL, 3, abc, &r, BOS) == ESNULLP);
    CHECK(r == 0);
    r = 123;
    CHECK(cplus_v__strcoll_s_chk(abc, 3, NULL, &r, BOS) == ESNULLP);
    CHECK(r == 0);
    CHECK(cplus_v__strcoll_s_chk(abc, 3, abc, NULL, BOS) == ESNULLP);

    /* Zero, max and one-over bounds. */
    r = 123;
    CHECK(cplus_v__strcoll_s_chk(abc, 0, abc, &r, BOS) == ESZEROL);
    CHECK(r == 0);
    r = 123;
    CHECK(cplus_v__strcoll_s_chk(abc, RSIZE_MAX_STR + 1, abc, &r, BOS) ==
          ESLEMAX);
    CHECK(r == 0);
    r = 123;
    CHECK(cplus_v__strcoll_s_chk(dest8, 4, dest8, &r, 3) == EOVERFLOW);
    CHECK(r == 0);
}

int main(void)
{
    test_strcmpfld();
    test_strcpyfld();
    test_strcoll();

    printf("vendor_field: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
