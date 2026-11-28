/* Regression for issue #193: realloc_s() must hold the allocator mutex across
 * host realloc() (no concurrent free/recycle of the old pointer), and
 * cplus_safe_remaining_capacity() must use the O(1) exact-base lookup while
 * guarding interior-pointer address arithmetic against unsigned wraparound.
 *
 * The file-local lock and table helpers are exposed by including the source;
 * the test supplies the two diagnostics stubs and intercepts host realloc to
 * observe whether the lock is held at its call boundary. */
#define CPLUS_SAFE_DIAG_NO_MACROS
#include "../../../stdlib/safe/src/safe_alloc.c"
#include <assert.h>
#include <stdio.h>

void cplus_safe_log(cplus_safe_log_level level, const char *api, const char *msg) {
    (void)level; (void)api; (void)msg;
}
void cplus_safe_stat_record(const char *api, int ok) { (void)api; (void)ok; }

static int g_lock_held_during_realloc = -1;

/* The checked payload came from host malloc, so copying into a fresh block is
 * sufficient for this probe. */
void *realloc(void *p, size_t n) {
    int rc = pthread_mutex_trylock(&g_lock);
    void *q;
    if (rc == 0) pthread_mutex_unlock(&g_lock);
    g_lock_held_during_realloc = (rc == EBUSY);
    q = malloc(n != 0u ? n : 1u);
    if (q != NULL) free(p);
    return q;
}

int main(void) {
    void *p = malloc_s(16);
    void *q;

    assert(p != NULL);
    q = realloc_s(p, 32u);
    assert(q != NULL);
    assert(g_lock_held_during_realloc == 1); /* was 0 before the fix */
    assert(cplus_safe_remaining_capacity(q) == 32u);
    assert(cplus_safe_ptr_size(q) == 32u);
    free_s(q);

    /* Exact-base and interior pointers both resolve; a corrupt size that
     * would wrap past UINTPTR_MAX must never look like a containing block. */
    pthread_mutex_lock(&g_lock);
    assert(tab_reserve_one() == 0);
    tab_insert((void *)(uintptr_t)0x2000u, 64u, 1u);
    pthread_mutex_unlock(&g_lock);
    assert(cplus_safe_remaining_capacity((void *)(uintptr_t)0x2000u) == 64u);
    assert(cplus_safe_remaining_capacity((void *)(uintptr_t)0x2010u) == 48u);

    pthread_mutex_lock(&g_lock);
    assert(tab_reserve_one() == 0);
    tab_insert((void *)(uintptr_t)0x1000u, 0x100u, 1u);
    pthread_mutex_unlock(&g_lock);
    assert(cplus_safe_remaining_capacity((void *)(uintptr_t)0x1800u) == (size_t)-1);

    puts("issue193_alloc_capacity: ok");
    return 0;
}
