/*
 * test_vendor_strchr.c -- standalone C99 test for the vendored rurban/safeclib
 * strchr_s / strrchr_s entry points in libc/src/safecrt_vendor/strchr_group.c.
 *
 * The upstream bodies are included verbatim in that object; only the public
 * names are prefixed to cplus_v_*. This test links the object directly because
 * the parent wires it into the checked runtime build separately:
 *
 *   /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
 *     -Ithird_party/safeclib/include -Ibuild/safe \
 *     -Ithird_party/safeclib/src -Istdlib/safe \
 *     -Wno-macro-redefined -c libc/src/safecrt_vendor/strchr_group.c \
 *     -o /tmp/strchr_group.o
 *   /usr/bin/clang -O2 -std=c99 -Ilibc/install-include \
 *     tests/libc/test_vendor_strchr.c /tmp/strchr_group.o \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -o /tmp/test_vendor_strchr
 *   timeout 30 /tmp/test_vendor_strchr
 *
 * Output: "vendor_strchr: <checks> <failures>"; exit code is nonzero on any
 * failure.
 */
#include "cplus_vendor_strchr.h"

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

#define BOS_UNKNOWN CPLUS_VENDOR_STRCHR_BOS_UNKNOWN
#define ONE_OVER    (CPLUS_VENDOR_STRCHR_RSIZE_MAX_STR + 1)

static void test_strchr_s(void)
{
    const char text[] = "aXbXcX";
    const char bounded[] = "abcX";
    char *r = NULL;
    int rc;

    /* Valid finds, compared against ordinary strchr(). */
    rc = cplus_v__strchr_s_chk(text, sizeof(text), 'X', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_EOK);
    CHECK(r == strchr(text, 'X'));
    CHECK(r == text + 1);

    r = NULL;
    rc = cplus_v__strchr_s_chk(text, sizeof(text), 'a', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_EOK);
    CHECK(r == strchr(text, 'a'));
    CHECK(r == text);

    /* The terminating NUL is a valid search target. */
    r = NULL;
    rc = cplus_v__strchr_s_chk(text, sizeof(text), '\0', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_EOK);
    CHECK(r == strchr(text, '\0'));
    CHECK(r == text + 6);

    /* Not found: ESNOTFND and a NULL result, matching strchr()'s NULL. */
    r = (char *)1;
    rc = cplus_v__strchr_s_chk(text, sizeof(text), 'z', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESNOTFND);
    CHECK(r == NULL);
    CHECK(strchr(text, 'z') == NULL);

    /* dmax is honored: a match beyond dmax is reported as not found. */
    r = (char *)1;
    rc = cplus_v__strchr_s_chk(bounded, 2, 'X', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESNOTFND);
    CHECK(r == NULL);
    r = NULL;
    rc = cplus_v__strchr_s_chk(bounded, sizeof(bounded), 'X', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_EOK);
    CHECK(r == strchr(bounded, 'X'));
    CHECK(r == bounded + 3);

    /* Constraint violations. */
    r = (char *)1;
    rc = cplus_v__strchr_s_chk(NULL, sizeof(text), 'X', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESNULLP);
    CHECK(r == NULL);

    rc = cplus_v__strchr_s_chk(text, sizeof(text), 'X', NULL, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESNULLP);

    r = (char *)1;
    rc = cplus_v__strchr_s_chk(text, 0, 'X', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESZEROL);
    CHECK(r == NULL);

    r = (char *)1;
    rc = cplus_v__strchr_s_chk(text, ONE_OVER, 'X', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESLEMAX);
    CHECK(r == NULL);

    r = (char *)1;
    rc = cplus_v__strchr_s_chk(text, sizeof(text), 300, &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESLEMAX);
    CHECK(r == NULL);
}

static void test_strrchr_s(void)
{
    const char text[] = "aXbXcX";
    const char bounded[] = "abcX";
    char *r = NULL;
    int rc;

    /* Valid finds must pick the LAST occurrence and match strrchr(). */
    rc = cplus_v__strrchr_s_chk(text, sizeof(text), 'X', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_EOK);
    CHECK(r == strrchr(text, 'X'));
    CHECK(r == text + 5);

    r = NULL;
    rc = cplus_v__strrchr_s_chk(text, sizeof(text), 'a', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_EOK);
    CHECK(r == strrchr(text, 'a'));
    CHECK(r == text);

    /* The NUL is found only when dmax reaches past the string length. */
    r = (char *)1;
    rc = cplus_v__strrchr_s_chk(text, 6, '\0', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESNOTFND);
    CHECK(r == NULL);
    r = NULL;
    rc = cplus_v__strrchr_s_chk(text, 7, '\0', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_EOK);
    CHECK(r == strrchr(text, '\0'));
    CHECK(r == text + 6);

    /* Not found. */
    r = (char *)1;
    rc = cplus_v__strrchr_s_chk(text, sizeof(text), 'z', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESNOTFND);
    CHECK(r == NULL);
    CHECK(strrchr(text, 'z') == NULL);

    /* dmax bounded search. */
    r = (char *)1;
    rc = cplus_v__strrchr_s_chk(bounded, 3, 'X', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESNOTFND);
    CHECK(r == NULL);
    r = NULL;
    rc = cplus_v__strrchr_s_chk(bounded, 4, 'X', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_EOK);
    CHECK(r == strrchr(bounded, 'X'));
    CHECK(r == bounded + 3);

    /* Constraint violations. */
    r = (char *)1;
    rc = cplus_v__strrchr_s_chk(NULL, sizeof(text), 'X', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESNULLP);
    CHECK(r == NULL);

    rc = cplus_v__strrchr_s_chk(text, sizeof(text), 'X', NULL, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESNULLP);

    r = (char *)1;
    rc = cplus_v__strrchr_s_chk(text, 0, 'X', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESZEROL);
    CHECK(r == NULL);

    r = (char *)1;
    rc = cplus_v__strrchr_s_chk(text, ONE_OVER, 'X', &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESLEMAX);
    CHECK(r == NULL);

    r = (char *)1;
    rc = cplus_v__strrchr_s_chk(text, sizeof(text), 300, &r, BOS_UNKNOWN);
    CHECK(rc == CPLUS_VENDOR_STRCHR_ESLEMAX);
    CHECK(r == NULL);
}

int main(void)
{
    test_strchr_s();
    test_strrchr_s();

    printf("vendor_strchr: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
