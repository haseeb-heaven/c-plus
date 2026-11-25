/*
 * test_vendor_firstlast.c -- standalone C99 test for the vendored safeclib
 * first/last char and diff helpers in libc/src/safecrt_vendor/firstlast_group.c.
 *
 * Build/link (run from the repo root):
 *   sh scripts/build_safecrt.sh >/dev/null 2>&1
 *   /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
 *     -Ithird_party/safeclib/include -Ibuild/safe \
 *     -Ithird_party/safeclib/src -Istdlib/safe \
 *     -Wno-macro-redefined -c libc/src/safecrt_vendor/firstlast_group.c \
 *     -o /tmp/firstlast_group.o
 *   /usr/bin/clang -O2 -std=c99 -Wall -Wextra -pedantic \
 *     -Ilibc/install-include \
 *     tests/libc/test_vendor_firstlast.c /tmp/firstlast_group.o \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -o /tmp/test_vendor_firstlast
 *   /tmp/test_vendor_firstlast
 */
#include "cplus_vendor_firstlast.h"

#include <stddef.h>
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

/* ---------------- local reference loops (never called with dmax == 0) ---- */

static int ref_firstchar(const char *s, size_t dmax, char c, size_t *idx)
{
    size_t i;
    for (i = 0; i < dmax && s[i] != '\0'; ++i) {
        if (s[i] == c) {
            *idx = i;
            return CPLUS_V_FIRSTLAST_EOK;
        }
    }
    *idx = 0;
    return CPLUS_V_FIRSTLAST_ESNOTFND;
}

static int ref_lastchar(const char *s, size_t dmax, char c, size_t *idx)
{
    size_t i;
    int found = 0;
    for (i = 0; i < dmax && s[i] != '\0'; ++i) {
        if (s[i] == c) {
            *idx = i;
            found = 1;
        }
    }
    if (!found) {
        *idx = 0;
    }
    return found ? CPLUS_V_FIRSTLAST_EOK : CPLUS_V_FIRSTLAST_ESNOTFND;
}

static int ref_diff(const char *a, const char *b, size_t dmax, int last,
                    size_t *idx)
{
    size_t i;
    int found = 0;
    for (i = 0; i < dmax && a[i] != '\0' && b[i] != '\0'; ++i) {
        if (a[i] != b[i]) {
            *idx = i;
            found = 1;
            if (!last) {
                return CPLUS_V_FIRSTLAST_EOK;
            }
        }
    }
    if (!found) {
        *idx = 0;
    }
    return found ? CPLUS_V_FIRSTLAST_EOK : CPLUS_V_FIRSTLAST_ESNODIFF;
}

/* ---------------- explicit found / not-found index cases ----------------- */

static void test_found_not_found(void)
{
    char dest[] = "abcabc";
    char *p = (char *)1;
    size_t idx = 999;
    int rc;

    rc = cplus_v__strfirstchar_s_chk(dest, sizeof(dest) - 1, 'a', &p,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_EOK);
    CHECK(p == dest + 0);

    p = (char *)1;
    rc = cplus_v__strfirstchar_s_chk(dest, sizeof(dest) - 1, 'c', &p,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_EOK);
    CHECK(p == dest + 2);

    p = (char *)1;
    rc = cplus_v__strfirstchar_s_chk(dest, sizeof(dest) - 1, 'z', &p,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNOTFND);
    CHECK(p == NULL);

    p = (char *)1;
    rc = cplus_v__strlastchar_s_chk(dest, sizeof(dest) - 1, 'a', &p,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_EOK);
    CHECK(p == dest + 3);

    p = (char *)1;
    rc = cplus_v__strlastchar_s_chk(dest, sizeof(dest) - 1, 'c', &p,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_EOK);
    CHECK(p == dest + 5);

    p = (char *)1;
    rc = cplus_v__strlastchar_s_chk(dest, sizeof(dest) - 1, 'z', &p,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNOTFND);
    CHECK(p == NULL);

    idx = 999;
    rc = cplus_v__strfirstdiff_s_chk(dest, sizeof(dest) - 1, "abcXbc", &idx,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_EOK);
    CHECK(idx == 3);

    idx = 999;
    rc = cplus_v__strlastdiff_s_chk(dest, sizeof(dest) - 1, "abcabX", &idx,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_EOK);
    CHECK(idx == 5);

    idx = 999;
    rc = cplus_v__strfirstdiff_s_chk(dest, sizeof(dest) - 1, "abcabc", &idx,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNODIFF);
    CHECK(idx == 0);

    idx = 999;
    rc = cplus_v__strlastdiff_s_chk(dest, sizeof(dest) - 1, "abcabc", &idx,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNODIFF);
    CHECK(idx == 0);

    /* One string is a prefix of the other: upstream stops at either NUL. */
    idx = 999;
    rc = cplus_v__strfirstdiff_s_chk(dest, sizeof(dest) - 1, "abc", &idx,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNODIFF);
    CHECK(idx == 0);
}

/* ---------------- NULL pointer constraints -------------------------------- */

static void test_null(void)
{
    char dest[] = "abcdef";
    char *p = (char *)1;
    size_t idx = 999;
    int rc;

    p = (char *)1;
    rc = cplus_v__strfirstchar_s_chk(NULL, 6, 'a', &p,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNULLP);
    CHECK(p == NULL);

    rc = cplus_v__strfirstchar_s_chk(dest, 6, 'a', NULL,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNULLP);

    p = (char *)1;
    rc = cplus_v__strlastchar_s_chk(NULL, 6, 'a', &p,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNULLP);
    CHECK(p == NULL);

    rc = cplus_v__strlastchar_s_chk(dest, 6, 'a', NULL,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNULLP);

    idx = 999;
    rc = cplus_v__strfirstdiff_s_chk(NULL, 6, "abcdef", &idx,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNULLP);
    CHECK(idx == 0);

    idx = 999;
    rc = cplus_v__strfirstdiff_s_chk(dest, 6, NULL, &idx,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNULLP);
    CHECK(idx == 0);

    rc = cplus_v__strfirstdiff_s_chk(dest, 6, "abcdef", NULL,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNULLP);

    idx = 999;
    rc = cplus_v__strlastdiff_s_chk(NULL, 6, "abcdef", &idx,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNULLP);
    CHECK(idx == 0);

    idx = 999;
    rc = cplus_v__strlastdiff_s_chk(dest, 6, NULL, &idx,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNULLP);
    CHECK(idx == 0);

    rc = cplus_v__strlastdiff_s_chk(dest, 6, "abcdef", NULL,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESNULLP);
}

/* ---------------- zero, one-over and max bounds --------------------------- */

static void test_bounds(void)
{
    char buf[8] = "abcdefg"; /* sizeof == 8, strlen == 7 */
    char *p = (char *)1;
    size_t idx = 999;
    int rc;

    p = (char *)1;
    rc = cplus_v__strfirstchar_s_chk(buf, 0, 'a', &p,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESZEROL);
    CHECK(p == NULL);

    p = (char *)1;
    rc = cplus_v__strlastchar_s_chk(buf, 0, 'a', &p,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESZEROL);
    CHECK(p == NULL);

    idx = 999;
    rc = cplus_v__strfirstdiff_s_chk(buf, 0, "abcdefg", &idx,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESZEROL);
    CHECK(idx == 0);

    idx = 999;
    rc = cplus_v__strlastdiff_s_chk(buf, 0, "abcdefg", &idx,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESZEROL);
    CHECK(idx == 0);

    /* dmax one greater than the known destination object size. */
    p = (char *)1;
    rc = cplus_v__strfirstchar_s_chk(buf, sizeof(buf) + 1, 'a', &p,
                                     sizeof(buf));
    CHECK(rc == CPLUS_V_FIRSTLAST_EOVERFLOW);
    CHECK(p == NULL);

    p = (char *)1;
    rc = cplus_v__strlastchar_s_chk(buf, sizeof(buf) + 1, 'a', &p,
                                    sizeof(buf));
    CHECK(rc == CPLUS_V_FIRSTLAST_EOVERFLOW);
    CHECK(p == NULL);

    idx = 999;
    rc = cplus_v__strfirstdiff_s_chk(buf, sizeof(buf) + 1, "abcdefg", &idx,
                                     sizeof(buf));
    CHECK(rc == CPLUS_V_FIRSTLAST_EOVERFLOW);
    CHECK(idx == 0);

    idx = 999;
    rc = cplus_v__strlastdiff_s_chk(buf, sizeof(buf) + 1, "abcdefg", &idx,
                                    sizeof(buf));
    CHECK(rc == CPLUS_V_FIRSTLAST_EOVERFLOW);
    CHECK(idx == 0);

    /* dmax one greater than RSIZE_MAX_STR, with unknown object size. */
    p = (char *)1;
    rc = cplus_v__strfirstchar_s_chk(buf, CPLUS_V_FIRSTLAST_RSIZE_MAX_STR + 1,
                                     'a', &p,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESLEMAX);
    CHECK(p == NULL);

    p = (char *)1;
    rc = cplus_v__strlastchar_s_chk(buf, CPLUS_V_FIRSTLAST_RSIZE_MAX_STR + 1,
                                    'a', &p,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESLEMAX);
    CHECK(p == NULL);

    idx = 999;
    rc = cplus_v__strfirstdiff_s_chk(buf, CPLUS_V_FIRSTLAST_RSIZE_MAX_STR + 1,
                                     "abcdefg", &idx,
                                     CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESLEMAX);
    CHECK(idx == 0);

    idx = 999;
    rc = cplus_v__strlastdiff_s_chk(buf, CPLUS_V_FIRSTLAST_RSIZE_MAX_STR + 1,
                                    "abcdefg", &idx,
                                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
    CHECK(rc == CPLUS_V_FIRSTLAST_ESLEMAX);
    CHECK(idx == 0);
}

/* ---------------- parity with local reference loops ----------------------- */

static void test_parity(void)
{
    static const char *const corpus[] = {
        "", "a", "ab", "abcabc", "aaaa", "abcaXabc", "xyz", "aXbXcX"
    };
    static const char chars[] = {'a', 'b', 'c', 'X', 'z', '\0'};
    size_t ci;
    size_t k;

    for (ci = 0; ci < sizeof(corpus) / sizeof(corpus[0]); ++ci) {
        char dest[32];
        size_t slen = strlen(corpus[ci]);
        size_t dmax;

        strcpy(dest, corpus[ci]);

        for (dmax = 1; dmax <= slen + 2; ++dmax) {
            for (k = 0; k < sizeof(chars); ++k) {
                char *p = (char *)1;
                size_t ref_idx = 999;
                int want = ref_firstchar(dest, dmax, chars[k], &ref_idx);
                int got = cplus_v__strfirstchar_s_chk(
                    dest, dmax, chars[k], &p,
                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
                CHECK(got == want);
                CHECK(p == (want == CPLUS_V_FIRSTLAST_EOK ? dest + ref_idx
                                                          : NULL));
            }
            for (k = 0; k < sizeof(chars); ++k) {
                char *p = (char *)1;
                size_t ref_idx = 999;
                int want = ref_lastchar(dest, dmax, chars[k], &ref_idx);
                int got = cplus_v__strlastchar_s_chk(
                    dest, dmax, chars[k], &p,
                    CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
                CHECK(got == want);
                CHECK(p == (want == CPLUS_V_FIRSTLAST_EOK ? dest + ref_idx
                                                          : NULL));
            }
        }
    }

    {
        static const char *const pairs[][2] = {
            {"", ""},
            {"a", "b"},
            {"abcabc", "abcabc"},
            {"abcabc", "abcXbc"},
            {"abcabc", "abcabX"},
            {"abcabc", "abc"},
            {"ab", "abcd"},
            {"aaa", "aXa"},
            {"aXbXcX", "aXbXcX"},
            {"aXbXcX", "aXbYcX"}
        };
        size_t pi;

        for (pi = 0; pi < sizeof(pairs) / sizeof(pairs[0]); ++pi) {
            char a[32];
            char b[32];
            size_t alen = strlen(pairs[pi][0]);
            size_t blen = strlen(pairs[pi][1]);
            size_t maxlen = alen > blen ? alen : blen;
            size_t dmax;

            strcpy(a, pairs[pi][0]);
            strcpy(b, pairs[pi][1]);

            for (dmax = 1; dmax <= maxlen + 2; ++dmax) {
                size_t ref_idx = 999;
                int want = ref_diff(a, b, dmax, 0, &ref_idx);
                size_t idx = 999;
                int got = cplus_v__strfirstdiff_s_chk(
                    a, dmax, b, &idx, CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
                CHECK(got == want);
                CHECK(idx == ref_idx);

                ref_idx = 999;
                want = ref_diff(a, b, dmax, 1, &ref_idx);
                idx = 999;
                got = cplus_v__strlastdiff_s_chk(
                    a, dmax, b, &idx, CPLUS_V_FIRSTLAST_BOS_UNKNOWN);
                CHECK(got == want);
                CHECK(idx == ref_idx);
            }
        }
    }
}

int main(void)
{
    test_found_not_found();
    test_null();
    test_bounds();
    test_parity();

    printf("vendor_firstlast: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
