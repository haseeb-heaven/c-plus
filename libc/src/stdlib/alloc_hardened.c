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
#include "alloc_internal.h"

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
    cplus_alloc_meta before;
    int rc;

    if (ptr == NULL) {
        errno = EINVAL;
        bump(&g_atomic_realloc_invalid);
        return EINVAL;
    }
    /* Zero-size reallocation has no unambiguous failure-atomic outcome here:
     * cplus_realloc maps it to a 1-byte block, while hosts that release the
     * block would leave a dangling *ptr with an EINVAL-style refusal. Refuse
     * it before touching either the slot or the allocation. */
    if (new_size == 0) {
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
    }

    /* The generation observed above is re-verified inside the allocator under
     * its own lock, across the host realloc and the metadata update. No other
     * thread can free or recycle old between the check and the operation. */
    rc = cplus_alloc_realloc_generation(ptr, new_size,
                                        old != NULL ? before.generation : 0);
    if (rc == CPLUS_ALLOC_GEN_OK) {
        bump(&g_atomic_realloc_ok);
        return 0;
    }
    if (rc == CPLUS_ALLOC_GEN_STALE) {
        bump(&g_atomic_realloc_inconsistent);
        errno = EINVAL;
        bump(&g_atomic_realloc_fail);
        return EINVAL;
    }
    if (rc == CPLUS_ALLOC_GEN_INVALID) {
        errno = EINVAL;
        bump(&g_atomic_realloc_invalid);
        return EINVAL;
    }
    errno = ENOMEM;
    bump(&g_atomic_realloc_fail);
    return ENOMEM;
}

int cplus_alloc_checked_free(void **ptr) {
    void *target;
    cplus_alloc_meta before;
    int rc;

    if (ptr == NULL || *ptr == NULL) {
        errno = EINVAL;
        bump(&g_checked_free_refused);
        return EINVAL;
    }

    target = *ptr;
    if (cplus_alloc_get_meta(target, &before) != 0 || before.is_live != 1) {
        /* Double free / stale / foreign pointer: refuse using metadata and
         * keep the caller's slot unchanged so the error is observable. */
        errno = EINVAL;
        bump(&g_checked_free_refused);
        return EINVAL;
    }

    /* One lock-protected check-and-act: the generation observed above must
     * still be live when the host free runs. On refusal the caller's slot is
     * untouched; only a successful free publishes NULL. */
    rc = cplus_alloc_free_generation(ptr, before.generation);
    if (rc != CPLUS_ALLOC_GEN_OK) {
        errno = EINVAL;
        bump(&g_checked_free_refused);
        return EINVAL;
    }
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
