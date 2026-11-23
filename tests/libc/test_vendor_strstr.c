/* Standalone C99 test for the vendored strstr_s / strcasestr_s group.
 *
 * Build (from repo root):
 *   /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
 *     -Ithird_party/safeclib/include -Ibuild/safe -Ithird_party/safeclib/src \
 *     -Istdlib/safe -Wno-macro-redefined \
 *     -c libc/src/safecrt_vendor/strstr_group.c -o /tmp/strstr_group.o
 *   /usr/bin/clang -O2 -std=c99 -Ilibc/install-include \
 *     tests/libc/test_vendor_strstr.c /tmp/strstr_group.o \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -o /tmp/test_vendor_strstr
 *
 * Prints: vendor_strstr: <checks> <failures>  (exit nonzero on failures). */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "cplus_vendor_strstr.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++checks;                                                              \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                     \
        }                                                                      \
    } while (0)

/* Force the runtime-check path (BOS unknown) for every call. */
#define U CPLUS_V_STRSTR_BOS_UNKNOWN

/* ASCII case-insensitive reference search, used only as an oracle. */
static const char *ci_find(const char *h, const char *n)
{
    size_t i;
    size_t hn = strlen(h);
    size_t nn = strlen(n);
    if (nn == 0)
        return h;
    if (nn > hn)
        return NULL;
    for (i = 0; i + nn <= hn; ++i) {
        size_t j;
        for (j = 0; j < nn; ++j) {
            if (tolower((unsigned char)h[i + j]) !=
                tolower((unsigned char)n[j]))
                break;
        }
        if (j == nn)
            return h + i;
    }
    return NULL;
}

/* ------------------------- strstr_s basics ------------------------- */
static void test_strstr_basics(void)
{
    char dest[64];
    char *sub;
    int rc;

    /* found */
    strcpy(dest, "hello world");
    sub = (char *)1;
    rc = cplus_v__strstr_s_chk(dest, 11, "world", 5, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_EOK);
    CHECK(sub == strstr(dest, "world"));
    CHECK(sub != NULL && strcmp(sub, "world") == 0);

    /* not found */
    strcpy(dest, "hello world");
    sub = (char *)1;
    rc = cplus_v__strstr_s_chk(dest, 11, "xyz", 3, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESNOTFND);
    CHECK(sub == NULL);

    /* NULL substringp: must not dereference, returns ESNULLP */
    strcpy(dest, "hello world");
    rc = cplus_v__strstr_s_chk(dest, 11, "world", 5, NULL, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESNULLP);

    /* NULL dest */
    sub = (char *)1;
    rc = cplus_v__strstr_s_chk(NULL, 11, "world", 5, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESNULLP);
    CHECK(sub == NULL);

    /* NULL src */
    strcpy(dest, "hello world");
    sub = (char *)1;
    rc = cplus_v__strstr_s_chk(dest, 11, NULL, 5, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESNULLP);
    CHECK(sub == NULL);

    /* dmax = 0 -> ESZEROL */
    strcpy(dest, "hello world");
    sub = (char *)1;
    rc = cplus_v__strstr_s_chk(dest, 0, "world", 5, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESZEROL);
    CHECK(sub == NULL);

    /* dmax one over RSIZE_MAX_STR -> ESLEMAX */
    sub = (char *)1;
    rc = cplus_v__strstr_s_chk(dest, CPLUS_V_STRSTR_RSIZE_MAX_STR + 1,
                               "world", 5, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESLEMAX);
    CHECK(sub == NULL);

    /* slen one over RSIZE_MAX_STR -> ESLEMAX */
    sub = (char *)1;
    rc = cplus_v__strstr_s_chk(dest, 11, "world",
                               CPLUS_V_STRSTR_RSIZE_MAX_STR + 1, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESLEMAX);
    CHECK(sub == NULL);

    /* slen = 0 with non-empty src and dest != src -> ESZEROL */
    strcpy(dest, "hello world");
    sub = (char *)1;
    rc = cplus_v__strstr_s_chk(dest, 11, "world", 0, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESZEROL);
    CHECK(sub == NULL);

    /* slen = 0 with empty src -> EOK, match is dest */
    strcpy(dest, "hello world");
    sub = NULL;
    rc = cplus_v__strstr_s_chk(dest, 11, "", 0, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_EOK);
    CHECK(sub == dest);

    /* dest == src -> EOK, match is dest */
    strcpy(dest, "hello world");
    sub = NULL;
    rc = cplus_v__strstr_s_chk(dest, 11, dest, 11, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_EOK);
    CHECK(sub == dest);

    /* one-over bounds: dmax = strlen(dest)+1 is still valid */
    strcpy(dest, "abc");
    sub = NULL;
    rc = cplus_v__strstr_s_chk(dest, 4, "bc", 2, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_EOK);
    CHECK(sub == dest + 1);

    /* slen > dmax and src longer than dest -> ESNOTFND */
    strcpy(dest, "abc");
    sub = (char *)1;
    rc = cplus_v__strstr_s_chk(dest, 3, "abcd", 4, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESNOTFND);
    CHECK(sub == NULL);

    /* truncated slen: only first slen chars of src are used */
    strcpy(dest, "abcdef");
    sub = NULL;
    rc = cplus_v__strstr_s_chk(dest, 6, "cdXX", 2, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_EOK);
    CHECK(sub == dest + 2);
}

/* ------------------- strstr_s parity with strstr ------------------- */
static void test_strstr_parity(void)
{
    static const char *const pairs[][2] = {
        { "abc", "a" }, { "abc", "c" }, { "abc", "b" },
        { "abc", "abc" }, { "abc", "abcd" }, { "abc", "z" },
        { "aaaa", "aa" }, { "aaaa", "aaa" }, { "aaaa", "b" },
        { "hello world", "world" }, { "hello world", "hello" },
        { "hello world", "o w" }, { "hello world", "lowo" },
        { "x", "" },
        { "a", "a" }, { "AbC", "b" }, { "AbC", "B" },
    };
    size_t k;
    for (k = 0; k < sizeof pairs / sizeof pairs[0]; ++k) {
        char dest[64];
        char *sub = (char *)1;
        const char *src = pairs[k][1];
        size_t dmax = strlen(pairs[k][0]);
        size_t slen = strlen(src);
        int rc;
        const char *expected = strstr(pairs[k][0], src);
        strcpy(dest, pairs[k][0]);
        rc = cplus_v__strstr_s_chk(dest, dmax, src, slen, &sub, U, U);
        if (expected != NULL) {
            CHECK(rc == CPLUS_V_STRSTR_EOK);
            CHECK(sub == dest + (expected - pairs[k][0]));
        } else {
            CHECK(rc == CPLUS_V_STRSTR_ESNOTFND);
            CHECK(sub == NULL);
        }
    }
}

/* ----------------------- strcasestr_s basics ----------------------- */
static void test_strcasestr_basics(void)
{
    char dest[64];
    char *sub;
    int rc;

    /* case-insensitive found */
    strcpy(dest, "Hello World");
    sub = (char *)1;
    rc = cplus_v__strcasestr_s_chk(dest, 11, "world", 5, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_EOK);
    CHECK(sub == dest + 6);
    CHECK(sub != NULL && strcmp(sub, "World") == 0);

    /* case-insensitive not found */
    strcpy(dest, "Hello World");
    sub = (char *)1;
    rc = cplus_v__strcasestr_s_chk(dest, 11, "xyz", 3, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESNOTFND);
    CHECK(sub == NULL);

    /* NULL substringp */
    strcpy(dest, "Hello World");
    rc = cplus_v__strcasestr_s_chk(dest, 11, "world", 5, NULL, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESNULLP);

    /* NULL dest */
    sub = (char *)1;
    rc = cplus_v__strcasestr_s_chk(NULL, 11, "world", 5, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESNULLP);
    CHECK(sub == NULL);

    /* NULL src */
    strcpy(dest, "Hello World");
    sub = (char *)1;
    rc = cplus_v__strcasestr_s_chk(dest, 11, NULL, 5, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESNULLP);
    CHECK(sub == NULL);

    /* dmax = 0 -> ESZEROL */
    sub = (char *)1;
    rc = cplus_v__strcasestr_s_chk(dest, 0, "world", 5, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESZEROL);
    CHECK(sub == NULL);

    /* dmax one over RSIZE_MAX_STR -> ESLEMAX */
    sub = (char *)1;
    rc = cplus_v__strcasestr_s_chk(dest, CPLUS_V_STRSTR_RSIZE_MAX_STR + 1,
                                   "world", 5, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESLEMAX);
    CHECK(sub == NULL);

    /* slen > dmax (slen <= RSIZE_MAX_STR) -> ESNOTFND */
    strcpy(dest, "abc");
    sub = (char *)1;
    rc = cplus_v__strcasestr_s_chk(dest, 3, "abcd", 4, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESNOTFND);
    CHECK(sub == NULL);

    /* slen > dmax AND slen > RSIZE_MAX_STR -> ESLEMAX */
    sub = (char *)1;
    rc = cplus_v__strcasestr_s_chk(dest, CPLUS_V_STRSTR_RSIZE_MAX_STR,
                                   "abcd", CPLUS_V_STRSTR_RSIZE_MAX_STR + 1,
                                   &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESLEMAX);
    CHECK(sub == NULL);

    /* slen = 0 -> ESZEROL (strcasestr_s has no empty-src carve-out) */
    strcpy(dest, "Hello World");
    sub = (char *)1;
    rc = cplus_v__strcasestr_s_chk(dest, 11, "", 0, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_ESZEROL);
    CHECK(sub == NULL);

    /* empty src with nonzero slen reaches the *src == '\0' early return */
    sub = NULL;
    rc = cplus_v__strcasestr_s_chk(dest, 11, "", 1, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_EOK);
    CHECK(sub == dest);

    /* dest == src -> EOK */
    sub = NULL;
    rc = cplus_v__strcasestr_s_chk(dest, 11, dest, 11, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_EOK);
    CHECK(sub == dest);

    /* one-over bounds: dmax = strlen(dest)+1 is still valid */
    strcpy(dest, "abc");
    sub = NULL;
    rc = cplus_v__strcasestr_s_chk(dest, 4, "B", 1, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_EOK);
    CHECK(sub == dest + 1);

    /* truncated slen, case-insensitive */
    strcpy(dest, "abcdef");
    sub = NULL;
    rc = cplus_v__strcasestr_s_chk(dest, 6, "CDxx", 2, &sub, U, U);
    CHECK(rc == CPLUS_V_STRSTR_EOK);
    CHECK(sub == dest + 2);
}

/* --------------- strcasestr_s parity with ci_find ------------------ */
static void test_strcasestr_parity(void)
{
    static const char *const pairs[][2] = {
        { "abc", "A" }, { "abc", "C" }, { "abc", "B" },
        { "abc", "ABC" }, { "abc", "abcd" }, { "abc", "z" },
        { "AbC", "b" }, { "AbC", "B" }, { "AbC", "abc" },
        { "hello world", "WORLD" }, { "hello world", "Hello" },
        { "hello world", "o W" }, { "hello world", "LOWO" },
        { "MiXeD", "xed" }, { "MiXeD", "XED" }, { "MiXeD", "q" },
        { "a", "A" }, { "a", "a" }, { "a", "aa" },
    };
    size_t k;
    for (k = 0; k < sizeof pairs / sizeof pairs[0]; ++k) {
        char dest[64];
        char *sub = (char *)1;
        const char *src = pairs[k][1];
        size_t dmax = strlen(pairs[k][0]);
        size_t slen = strlen(src);
        int rc;
        const char *expected = ci_find(pairs[k][0], src);
        strcpy(dest, pairs[k][0]);
        rc = cplus_v__strcasestr_s_chk(dest, dmax, src, slen, &sub, U, U);
        if (expected != NULL) {
            CHECK(rc == CPLUS_V_STRSTR_EOK);
            CHECK(sub == dest + (expected - pairs[k][0]));
        } else {
            CHECK(rc == CPLUS_V_STRSTR_ESNOTFND);
            CHECK(sub == NULL);
        }
    }
}

int main(void)
{
    CHECK(sizeof(size_t) > 0); /* sanity: at least one check */
    test_strstr_basics();
    test_strstr_parity();
    test_strcasestr_basics();
    test_strcasestr_parity();
    printf("vendor_strstr: %d %d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
