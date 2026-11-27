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

/* Generation-aware primitives from alloc.c. They execute the entire
 * validate/host-operation/proof sequence under the allocator's metadata
 * lock, closing the check-then-act race in this wrapper layer. */
extern int cplus_alloc_realloc_proof(void **ptr, size_t new_size);
extern int cplus_alloc_free_verified(void *target);

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
    int rc;

    if (ptr == NULL) {
        errno = EINVAL;
        bump(&g_atomic_realloc_invalid);
        return EINVAL;
    }

    /* The check, realloc and failure proof share one allocator lock; no
     * separate metadata snapshot can go stale between them. */
    rc = cplus_alloc_realloc_proof(ptr, new_size);
    if (rc == -1) {
        bump(&g_atomic_realloc_inconsistent);
        errno = EINVAL;
        bump(&g_atomic_realloc_fail);
        return EINVAL;
    }
    if (rc != 0) {
        bump(&g_atomic_realloc_fail);
        return rc;
    }
    bump(&g_atomic_realloc_ok);
    return 0;
}

int cplus_alloc_checked_free(void **ptr) {
    void *target;

    if (ptr == NULL || *ptr == NULL) {
        errno = EINVAL;
        bump(&g_checked_free_refused);
        return EINVAL;
    }

    target = *ptr;
    /* Validate generation, retire metadata and release the block atomically:
     * a concurrent free/reuse cannot slip between a pre-check and the free. */
    if (cplus_alloc_free_verified(target) != 0) {
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
