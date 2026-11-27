/* Deterministic regression for issue #206: recent_remove() must leave a
 * tombstone in the open-addressed g_recent[] table. When a freed pointer A and
 * a later freed pointer B hash to the same bucket, reallocating A truncates
 * B's probe chain if A's slot is set to NULL; the subsequent double free of B
 * is then misclassified as an invalid free.
 *
 * Output: "recent_tombstone_attack: <checks> <failures>".
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "cplus_libc.h"
extern int cplus_alloc_meta_register(void *p, size_t bytes, size_t elem_size);

static int g_checks;
static int g_failures;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) { ++g_failures; fprintf(stderr, "FAIL: %s\n", (name)); }  \
    } while (0)

#define RECENT_CAP 1024u

static size_t recent_index(const void *p) {
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= (uintptr_t)0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x & (RECENT_CAP - 1u);
}

int main(void)
{
    enum { N = 4096 };
    void *v[N];
    void *a = NULL, *b = NULL;
    size_t ia = 0, ib, i;
    unsigned long df, inv;
    void *alias;

    for (i = 0; i < N; ++i) {
        v[i] = cplus_malloc(32);
        CHECK(v[i] != NULL, "collision probe allocation");
        if (v[i] == NULL) break;
        if (a == NULL) { a = v[i]; ia = recent_index(a); }
        else if (b == NULL && recent_index(v[i]) == ia) { b = v[i]; ib = recent_index(b); (void)ib; break; }
    }
    CHECK(b != NULL, "found same-bucket freed pointer pair");
    if (b == NULL) {
        for (i = 0; i < N; ++i) if (v[i] != NULL) cplus_free(v[i]);
        printf("recent_tombstone_attack: %d %d\n", g_checks, g_failures);
        return 1;
    }

    /* Free A then B: A occupies the home bucket, B the next probe slot. */
    cplus_free(a);
    alias = b;
    CHECK(cplus_free_checked(&alias) == 0, "free colliding B");
    CHECK(alias == NULL, "checked free NULLs B slot");

    df = cplus_alloc_double_free_count();
    inv = cplus_alloc_invalid_free_count();

    /* Reallocate A at the same address: meta_insert() -> recent_remove(A).
     * With NULL instead of a tombstone, recent_find(B) stops at A's slot. */
    alias = a;
    CHECK(cplus_alloc_is_live(a) == 0, "A is recently freed");
    /* Register a fresh allocation at exactly A's address. meta_insert() calls
     * recent_remove(A); the host free-list policy need not hand back the same
     * address, so registration is used to make the collision deterministic. */
    CHECK(cplus_alloc_meta_register(a, 32u, 1u) == 0,
          "fresh allocation registered at A address");
    CHECK(cplus_alloc_is_live(a) == 1, "A is live again after reuse");

    /* Double-free B must still be recognised as a double free. */
    alias = b;
    errno = 0;
    CHECK(cplus_free_checked(&alias) != 0, "double free of B refused");
    CHECK(errno == EINVAL, "double free sets EINVAL");
    CHECK(cplus_alloc_double_free_count() == df + 1u,
          "colliding B counted as double free, not invalid free");
    CHECK(cplus_alloc_invalid_free_count() == inv,
          "no invalid-free misclassification");

    /* Cleanup live blocks (A is live; all other probe allocations too). */
    for (i = 0; i < N; ++i) {
        /* a was physically freed once and then re-registered as a host block
         * for the collision test; it must not be released a second time. */
        if (v[i] == NULL || v[i] == b || v[i] == a) continue;
        cplus_free(v[i]);
    }
    printf("recent_tombstone_attack: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
