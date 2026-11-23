/* Additive failure-atomic allocation wrappers.
 *
 * Built separately from alloc.c (which is read-only for this change) and
 * linked beside build/libc/libcplus_hardened.a. Everything here is expressed
 * through the existing public metadata APIs; no allocator internals, pointer
 * tags, or layout details are duplicated.
 */
#define _POSIX_C_SOURCE 200112L
#include "cplus_alloc_hardened.h"

#include <errno.h>
#include <pthread.h>

#include "cplus_libc.h"

/* Best-effort diagnostic counters. They are independent of the allocator's
 * counters in alloc.c and only describe wrapper activity. */
static pthread_mutex_t g_hardened_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long g_atomic_realloc_ok = 0;
static unsigned long g_atomic_realloc_fail = 0;
static unsigned long g_atomic_realloc_invalid = 0;
static unsigned long g_atomic_realloc_inconsistent = 0;
static unsigned long g_checked_free_ok = 0;
static unsigned long g_checked_free_refused = 0;
static unsigned long g_state_dumps = 0;

static void bump(unsigned long *counter) {
    pthread_mutex_lock(&g_hardened_lock);
    ++*counter;
    pthread_mutex_unlock(&g_hardened_lock);
}

int cplus_alloc_atomic_realloc(void **ptr, size_t new_size) {
    void *old;
    void *fresh;
    cplus_alloc_meta before;
    cplus_alloc_meta after;
    int have_before = 0;
    int saved;

    if (ptr == NULL) {
        errno = EINVAL;
        bump(&g_atomic_realloc_invalid);
        return EINVAL;
    }

    old = *ptr;
    if (old != NULL) {
        /* Untracked/freed pointers are refused before the underlying
         * realloc can touch them; *ptr is deliberately left as-is. */
        if (cplus_alloc_get_meta(old, &before) != 0 || before.is_live != 1) {
            errno = EINVAL;
            bump(&g_atomic_realloc_invalid);
            return EINVAL;
        }
        have_before = 1;
    }

    fresh = cplus_realloc(old, new_size);
    if (fresh == NULL) {
        saved = (errno != 0) ? errno : ENOMEM;
        if (have_before) {
            /* Prove failure atomicity against the metadata side table: the
             * original block must still be the same live allocation. */
            if (cplus_alloc_get_meta(old, &after) != 0 ||
                after.is_live != 1 ||
                after.byte_size != before.byte_size ||
                after.generation != before.generation) {
                bump(&g_atomic_realloc_inconsistent);
                errno = EINVAL;
                bump(&g_atomic_realloc_fail);
                return EINVAL;
            }
        }
        errno = saved; /* ENOMEM from injection/host, EINVAL if reported */
        bump(&g_atomic_realloc_fail);
        return (saved == EINVAL) ? EINVAL : ENOMEM;
    }

    /* Only publish the new address after the underlying realloc succeeded. */
    *ptr = fresh;
    bump(&g_atomic_realloc_ok);
    return 0;
}

int cplus_alloc_checked_free(void **ptr) {
    void *target;
    void *tmp;

    if (ptr == NULL || *ptr == NULL) {
        errno = EINVAL;
        bump(&g_checked_free_refused);
        return EINVAL;
    }

    target = *ptr;
    if (cplus_alloc_is_live(target) != 1) {
        /* Double free / stale / foreign pointer: refuse using metadata and
         * keep the caller's slot unchanged so the error is observable. */
        errno = EINVAL;
        bump(&g_checked_free_refused);
        return EINVAL;
    }

    /* Use a local copy so a refused free inside cplus_free_checked cannot
     * NULL the caller's pointer; only a successful free publishes NULL. */
    tmp = target;
    if (cplus_free_checked(&tmp) != 0) {
        errno = EINVAL;
        bump(&g_checked_free_refused);
        return EINVAL;
    }

    *ptr = NULL;
    bump(&g_checked_free_ok);
    return 0;
}

void cplus_alloc_state_dump(FILE *stream) {
    unsigned long ok_r, fail_r, invalid_r, inconsistent;
    unsigned long ok_f, refused_f, dumps;

    if (stream == NULL)
        return;

    pthread_mutex_lock(&g_hardened_lock);
    ok_r = g_atomic_realloc_ok;
    fail_r = g_atomic_realloc_fail;
    invalid_r = g_atomic_realloc_invalid;
    inconsistent = g_atomic_realloc_inconsistent;
    ok_f = g_checked_free_ok;
    refused_f = g_checked_free_refused;
    ++g_state_dumps;
    dumps = g_state_dumps;
    pthread_mutex_unlock(&g_hardened_lock);

    fputs("cplus_alloc_state_dump:\n", stream);
    fprintf(stream, "  live_count=%lu\n", (unsigned long)cplus_alloc_live_count());
    fprintf(stream, "  live_bytes=%lu\n", (unsigned long)cplus_alloc_live_bytes());
    fprintf(stream, "  double_frees=%lu\n", (unsigned long)cplus_alloc_double_free_count());
    fprintf(stream, "  invalid_frees=%lu\n", (unsigned long)cplus_alloc_invalid_free_count());
    fprintf(stream, "  atomic_realloc_ok=%lu\n", ok_r);
    fprintf(stream, "  atomic_realloc_fail=%lu\n", fail_r);
    fprintf(stream, "  atomic_realloc_invalid=%lu\n", invalid_r);
    fprintf(stream, "  atomic_realloc_inconsistent=%lu\n", inconsistent);
    fprintf(stream, "  checked_free_ok=%lu\n", ok_f);
    fprintf(stream, "  checked_free_refused=%lu\n", refused_f);
    fprintf(stream, "  state_dumps=%lu\n", dumps);
}
