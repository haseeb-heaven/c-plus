/*
 * test_safecrt_upstream.c -- standalone C99 test for the additional vendored
 * safeclib ports in libc/src/string/upstream_safecrt.c.
 *
 * Compiled directly against the new object (the parent wires the source into
 * the build later):
 *   /usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
 *     -Ilibc/install-include \
 *     tests/libc/test_safecrt_upstream.c \
 *     <build>/upstream_safecrt.o build/libc/libcplus_hardened.a \
 *     -o <out>
 */
#include "cplus_safecrt_upstream.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

/* Comparator actually reads its context: mode 0 = ascending array,
 * mode 1 = descending array. This proves the context argument is threaded
 * through to compar by the port. */
static int cmp_int(const void *k, const void *e, void *ctx)
{
    int mode = *(int *)ctx;
    int a = *(const int *)k;
    int b = *(const int *)e;
    int r = (a > b) - (a < b);
    return mode ? -r : r;
}

static void test_bsearch_s(void)
{
    int asc[] = {1, 3, 5, 7, 9, 11};
    int desc[] = {11, 9, 7, 5, 3, 1};
    int ascending = 0;
    int descending = 1;
    int key = 7;
    int missing = 8;
    void *r;

    r = cplus_sc_upstream_bsearch_s(&key, asc, 6, sizeof(asc[0]), cmp_int,
                                    &ascending);
    CHECK(r != NULL);
    CHECK(r != NULL && *(int *)r == 7);
    CHECK(errno == 0);

    /* Same key, reversed array + context selects reversed comparison. */
    r = cplus_sc_upstream_bsearch_s(&key, desc, 6, sizeof(desc[0]), cmp_int,
                                    &descending);
    CHECK(r != NULL);
    CHECK(r != NULL && *(int *)r == 7);

    /* Not found: clean NULL, errno zeroed exactly as upstream does. */
    errno = 123;
    r = cplus_sc_upstream_bsearch_s(&missing, asc, 6, sizeof(asc[0]), cmp_int,
                                    &ascending);
    CHECK(r == NULL);
    CHECK(errno == 0);

    /* nmemb == 0 is not a constraint violation for bsearch_s. */
    errno = 99;
    r = cplus_sc_upstream_bsearch_s(&key, asc, 0, sizeof(asc[0]), cmp_int,
                                    &ascending);
    CHECK(r == NULL);
    CHECK(errno == 0);

    /* Constraint violations: null key/base/compar and oversized size. */
    errno = 0;
    r = cplus_sc_upstream_bsearch_s(NULL, asc, 6, sizeof(asc[0]), cmp_int,
                                    &ascending);
    CHECK(r == NULL);
    CHECK(errno == CPLUS_SC_UPSTREAM_ESNULLP);

    errno = 0;
    r = cplus_sc_upstream_bsearch_s(&key, NULL, 6, sizeof(asc[0]), cmp_int,
                                    &ascending);
    CHECK(r == NULL);
    CHECK(errno == CPLUS_SC_UPSTREAM_ESNULLP);

    errno = 0;
    r = cplus_sc_upstream_bsearch_s(&key, asc, 6, sizeof(asc[0]), NULL,
                                    &ascending);
    CHECK(r == NULL);
    CHECK(errno == CPLUS_SC_UPSTREAM_ESNULLP);

    errno = 0;
    r = cplus_sc_upstream_bsearch_s(
        &key, asc, 6, CPLUS_SC_UPSTREAM_RSIZE_MAX_MEM + 1, cmp_int, &ascending);
    CHECK(r == NULL);
    CHECK(errno == CPLUS_SC_UPSTREAM_ESLEMAX);
}

static void test_memrchr_s(void)
{
    const char text[] = "aXbXcX";
    char with_nul[4];
    void *r = (void *)1;
    int rc;

    /* Last occurrence, not the first -- this is what distinguishes memrchr
     * from memchr. */
    rc = cplus_sc_upstream_memrchr_s(text, strlen(text), 'X', &r);
    CHECK(rc == CPLUS_SC_UPSTREAM_EOK);
    CHECK(r == text + 5);

    /* The terminating NUL is a valid search target inside dmax. */
    with_nul[0] = 'a';
    with_nul[1] = '\0';
    with_nul[2] = 'b';
    with_nul[3] = '\0';
    rc = cplus_sc_upstream_memrchr_s(with_nul, sizeof(with_nul), '\0', &r);
    CHECK(rc == CPLUS_SC_UPSTREAM_EOK);
    CHECK(r == with_nul + 3);

    /* Not found clears resultp. */
    r = (void *)1;
    rc = cplus_sc_upstream_memrchr_s(text, strlen(text), 'z', &r);
    CHECK(rc == CPLUS_SC_UPSTREAM_ESNOTFND);
    CHECK(r == NULL);

    /* Constraint violations. */
    rc = cplus_sc_upstream_memrchr_s(text, strlen(text), 'X', NULL);
    CHECK(rc == CPLUS_SC_UPSTREAM_ESNULLP);

    r = (void *)1;
    rc = cplus_sc_upstream_memrchr_s(NULL, 3, 'X', &r);
    CHECK(rc == CPLUS_SC_UPSTREAM_ESNULLP);
    CHECK(r == NULL);

    r = (void *)1;
    rc = cplus_sc_upstream_memrchr_s(text, 0, 'X', &r);
    CHECK(rc == CPLUS_SC_UPSTREAM_ESZEROL);
    CHECK(r == NULL);

    r = (void *)1;
    rc = cplus_sc_upstream_memrchr_s(text, strlen(text), 300, &r);
    CHECK(rc == CPLUS_SC_UPSTREAM_ESLEMAX);
    CHECK(r == NULL);

    r = (void *)1;
    rc = cplus_sc_upstream_memrchr_s(text, CPLUS_SC_UPSTREAM_RSIZE_MAX_MEM + 1,
                                     'X', &r);
    CHECK(rc == CPLUS_SC_UPSTREAM_ESLEMAX);
    CHECK(r == NULL);
}

int main(void)
{
    test_bsearch_s();
    test_memrchr_s();

    if (g_failures != 0) {
        printf("test_safecrt_upstream: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_safecrt_upstream: all checks passed\n");
    return 0;
}
