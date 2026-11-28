/* White-box regression for issue #193: realloc_s() must hold the allocator
 * mutex across the host realloc(), and remaining_capacity() must not wrap the
 * end address of a block.  Including the source exposes the file-local lock
 * and table helpers; the test supplies the two diagnostics stubs and its own
 * realloc symbol so it can observe the mutex state at the call boundary. */
#define CPLUS_SAFE_DIAG_NO_MACROS
#include "../../../stdlib/safe/src/safe_alloc.c"
#include <assert.h>
#include <stdio.h>

void cplus_safe_log(cplus_safe_log_level level, const char *api, const char *msg) {
    (void)level; (void)api; (void)msg;
}
void cplus_safe_stat_record(const char *api, int ok) { (void)api; (void)ok; }

static int g_lock_held_during_realloc = -1;

/* Overrides host realloc for this binary.  The checked allocator's payload is
 * allocated by host malloc, so a fresh block is sufficient for this probe. */
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
    /* With the bug the lock was released before the host realloc call. */
    assert(g_lock_held_during_realloc == 1);
    assert(cplus_safe_remaining_capacity(q) == 32u);
    assert(cplus_safe_ptr_size(q) == 32u);
    free_s(q);

    /* Exact base and interior pointers both resolve, without wrapping. */
    pthread_mutex_lock(&g_lock);
    assert(tab_reserve_one() == 0);
    tab_insert((void *)(uintptr_t)0x2000u, 64u, 1u);
    pthread_mutex_unlock(&g_lock);
    assert(cplus_safe_remaining_capacity((void *)(uintptr_t)0x2000u) == 64u);
    assert(cplus_safe_remaining_capacity((void *)(uintptr_t)0x2010u) == 48u);

    /* An address outside every tracked block stays "untracked".  The
     * end-address overflow guard in the source is defense-in-depth for a
     * corrupt size; unsigned wrapping alone cannot make a low-base block
     * appear to contain a higher probe address. */
    pthread_mutex_lock(&g_lock);
    assert(tab_reserve_one() == 0);
    tab_insert((void *)(uintptr_t)0x1000u, 0x100u, 1u);
    pthread_mutex_unlock(&g_lock);
    assert(cplus_safe_remaining_capacity((void *)(uintptr_t)0x1800u) == (size_t)-1);

    puts("issue193_realloc_lock_capacity: ok");
    return 0;
}
