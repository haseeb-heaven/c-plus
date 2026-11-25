/* Regression test for the ordinary-name `_s` macro mappings added to
 * stdlib/safe/safe_c.h: memrchr_s and bsearch_s. It also exercises the
 * pre-existing strtok_s/strerror_s mappings from safe_string.h.
 *
 * Every mapped target is a symbol that already exists in the shipped archives
 * (build/libc/libcplus_hardened.a and build/safe/libcplus_safecrt.a). This test
 * declares no new functions and adds no wrappers. The exact compile/link
 * command is recorded in docs/libc/SAFE_C_NEW_MAPPINGS.md.
 */
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* safeclib ESLEMIN is not re-exported by cplus_safecrt_upstream.h. */
#define CPLUS_TEST_ESLEMIN 402

/* bsearch_s comparator: 3-arg (key, element, context). */
static int cmp_ctx(const void *key, const void *elem, void *ctx) {
    int a, b;
    (void)ctx;
    a = *(const int *)key;
    b = *(const int *)elem;
    return (a > b) - (a < b);
}

/* Ordinary bsearch comparator: 2-arg, used only for equivalence checks. */
static int cmp_plain(const void *key, const void *elem) {
    int a = *(const int *)key;
    int b = *(const int *)elem;
    return (a > b) - (a < b);
}

/* memrchr_s: compiled cplus_sc_upstream_memrchr_s; EOK/ESNOTFND/ESNULLP/
 * ESZEROL/ESLEMAX/ESNOSPC plus the __builtin_object_size one-over guard. */
static void test_memrchr_s(void) {
    char text[] = "abcXdefX";
    void *got = NULL;
    void *expected;
    int rc;

    /* Valid input: the 3-arg form infers dmax = BOS(text) = sizeof(text)-1. */
    rc = memrchr_s(text, 'X', &got);
    expected = (void *)cplus_memrchr(text, 'X', sizeof(text) - 1);
    assert(rc == CPLUS_SC_UPSTREAM_EOK);
    assert(got == expected);
    assert((char *)got == text + 7);

    /* Valid input: the explicit-dmax form agrees with the ordinary spelling. */
    got = NULL;
    assert(memrchr_s(text, (rsize_t)(sizeof(text) - 1), 'X', &got) ==
           CPLUS_SC_UPSTREAM_EOK);
    assert(got == expected);

    /* Clean not-found. */
    got = (void *)text;
    assert(memrchr_s(text, (rsize_t)(sizeof(text) - 1), 'Z', &got) ==
           CPLUS_SC_UPSTREAM_ESNOTFND);
    assert(got == NULL);

    /* NULL destination and NULL result parameter. */
    got = (void *)text;
    assert(memrchr_s(NULL, 4, 'Q', &got) == CPLUS_SC_UPSTREAM_ESNULLP);
    assert(got == NULL);
    assert(memrchr_s(text, 4, 'Q', NULL) == CPLUS_SC_UPSTREAM_ESNULLP);

    /* Zero length. */
    got = (void *)text;
    assert(memrchr_s(text, 0, 'Q', &got) == CPLUS_SC_UPSTREAM_ESZEROL);
    assert(got == NULL);

    /* ch > 255. */
    assert(memrchr_s(text, (rsize_t)(sizeof(text) - 1), 300, &got) ==
           CPLUS_SC_UPSTREAM_ESLEMAX);

    /* Exact fit and one-over against a known stack object. */
    {
        char block[8];
        void *r = NULL;
        size_t i;
        for (i = 0; i < sizeof(block); ++i)
            block[i] = 'A';
        block[4] = 'Q';

        r = NULL;
        assert(memrchr_s(block, (rsize_t)sizeof(block), 'Q', &r) ==
               CPLUS_SC_UPSTREAM_EOK);
        assert(r == block + 4);

        /* dmax = sizeof(block)+1 would read one byte past the object. The
         * macro's BOS guard refuses it with ESNOSPC (406) instead. */
        r = (void *)block;
        assert(memrchr_s(block, (rsize_t)(sizeof(block) + 1), 'Q', &r) ==
               CPLUS_SC_UPSTREAM_ESNOSPC);
        assert(r == NULL);
    }
}

/* bsearch_s: compiled cplus_sc_upstream_bsearch_s; errno 0/ESNULLP/ESLEMAX,
 * plus the macro's ESNOSPC (406) guard for nmemb*size > BOS(base). */
static void test_bsearch_s(void) {
    int arr[] = {1, 3, 5, 7, 9, 11};
    const size_t n = sizeof(arr) / sizeof(arr[0]);
    int key = 7;
    void *got;
    void *expected;

    errno = CPLUS_SC_UPSTREAM_ESLEMAX;
    got = bsearch_s(&key, arr, n, sizeof(arr[0]), cmp_ctx, NULL);
    expected = bsearch(&key, arr, n, sizeof(arr[0]), cmp_plain);
    assert(errno == CPLUS_SC_UPSTREAM_EOK);
    assert(got == expected);
    assert(*(int *)got == 7);

    /* Clean not-found agrees with the ordinary spelling. */
    key = 8;
    errno = CPLUS_SC_UPSTREAM_ESLEMAX;
    got = bsearch_s(&key, arr, n, sizeof(arr[0]), cmp_ctx, NULL);
    expected = bsearch(&key, arr, n, sizeof(arr[0]), cmp_plain);
    assert(got == NULL && expected == NULL);
    assert(errno == CPLUS_SC_UPSTREAM_EOK);

    /* NULL key, base and comparator. */
    key = 7;
    errno = 0;
    assert(bsearch_s(NULL, arr, n, sizeof(arr[0]), cmp_ctx, NULL) == NULL &&
           errno == CPLUS_SC_UPSTREAM_ESNULLP);
    errno = 0;
    assert(bsearch_s(&key, NULL, n, sizeof(arr[0]), cmp_ctx, NULL) == NULL &&
           errno == CPLUS_SC_UPSTREAM_ESNULLP);
    errno = 0;
    assert(bsearch_s(&key, arr, n, sizeof(arr[0]), NULL, NULL) == NULL &&
           errno == CPLUS_SC_UPSTREAM_ESNULLP);

    /* Zero count is a clean not-found. */
    errno = CPLUS_SC_UPSTREAM_ESLEMAX;
    assert(bsearch_s(&key, arr, 0, sizeof(arr[0]), cmp_ctx, NULL) == NULL &&
           errno == CPLUS_SC_UPSTREAM_EOK);

    /* Exact fit: 8 * sizeof(int) == BOS(block). */
    {
        int block[8];
        size_t i;
        for (i = 0; i < 8; ++i)
            block[i] = (int)(i * 2 + 1);
        key = 3;
        errno = CPLUS_SC_UPSTREAM_ESLEMAX;
        got = bsearch_s(&key, block, 8, sizeof(block[0]), cmp_ctx, NULL);
        assert(got != NULL && *(int *)got == 3);
        assert(errno == CPLUS_SC_UPSTREAM_EOK);

        /* One-over: 9 * 4 = 36 > sizeof(block) = 32 -> ESNOSPC (406). */
        errno = 0;
        assert(bsearch_s(&key, block, 9, sizeof(block[0]), cmp_ctx, NULL) == NULL &&
               errno == CPLUS_SC_UPSTREAM_ESNOSPC);
    }
}

/* strtok_s / strerror_s were already mapped in safe_string.h; keep them
 * covered here so the full `_s` mapping surface has one regression test. */
static void test_preexisting_mappings(void) {
    char work1[] = "one two three";
    char work2[sizeof(work1)];
    char *save = NULL;
    char *t1, *t2;
    rsize_t dmax = sizeof(work1);
    char errbuf[64];
    size_t need;

    memcpy(work2, work1, sizeof(work1));

    t1 = strtok_s(work1, &dmax, " ", &save);
    t2 = strtok(work2, " ");
    assert(t1 != NULL && t2 != NULL && strcmp(t1, t2) == 0);
    assert(strcmp(t1, "one") == 0);

    t1 = strtok_s(NULL, &dmax, " ", &save);
    t2 = strtok(NULL, " ");
    assert(t1 != NULL && t2 != NULL && strcmp(t1, t2) == 0);

    /* Fresh context is required for a NULL-destination constraint check;
     * a non-NULL context means "continue the previous tokenisation". */
    errno = 0;
    save = NULL;
    dmax = sizeof(work1);
    assert(strtok_s(NULL, &dmax, " ", &save) == NULL &&
           errno == CPLUS_SC_UPSTREAM_ESNULLP);

    errno = 0;
    save = NULL;
    dmax = 0;
    assert(strtok_s(work2, &dmax, " ", &save) == NULL &&
           errno == CPLUS_SC_UPSTREAM_ESZEROL);

    /* strerror_s valid input matches the ordinary spelling. */
    need = strlen(strerror(EINVAL)) + 1;
    assert(strerror_s(errbuf, (rsize_t)sizeof(errbuf), EINVAL) ==
           CPLUS_SC_UPSTREAM_EOK);
    assert(strcmp(errbuf, strerror(EINVAL)) == 0);

    /* Exact fit. */
    {
        char exact[64];
        assert(strerror_s(exact, (rsize_t)need, EINVAL) == CPLUS_SC_UPSTREAM_EOK);
        assert(strcmp(exact, strerror(EINVAL)) == 0);
    }

    /* NULL, zero and too-small destinations. */
    assert(strerror_s(NULL, (rsize_t)sizeof(errbuf), EINVAL) ==
           CPLUS_SC_UPSTREAM_ESNULLP);
    assert(strerror_s(errbuf, 0, EINVAL) == CPLUS_SC_UPSTREAM_ESZEROL);
    assert(strerror_s(errbuf, 2, EINVAL) == CPLUS_TEST_ESLEMIN);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    cplus_runtime_set_violation_trap(0);

    test_memrchr_s();
    test_bsearch_s();
    test_preexisting_mappings();

    printf("safe_c_mapping: ok\n");
    return 0;
}
