/* Single definition of the thread-local scratch stack for the modern C+
 * DataStack allocator.  Kept dependency-free so it builds with plain clang in
 * the hardened-libc build; stdlib/datastack.hp declares the matching extern.
 * The field order/types must mirror stdlib_datastack in that header.
 *
 * __thread by itself has no destructor: a worker that touched the DataStack
 * leaked its backing reservation when it exited.  We register a pthread key
 * whose destructor releases that backing region, and arm the key for every
 * thread created through the hardened cplus_pthread_create() wrapper (plus
 * the initial thread via a constructor).  The public TLS layout is unchanged.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "datastack_tls_internal.h"

typedef struct stdlib_datastack_tls {
    unsigned char *base;
    unsigned char *top;
    size_t capacity;
    size_t committed;
    uint64_t generation;
    int backing;
    size_t page;
} stdlib_datastack_tls;

__thread stdlib_datastack_tls stdlib_tls_datastack;

#ifndef _WIN32
#include <sys/mman.h>

static pthread_key_t g_datastack_key;
static pthread_once_t g_datastack_once = PTHREAD_ONCE_INIT;
static int g_datastack_key_ok;

/* The key's value is only a non-NULL sentinel; the destructor reads the
 * thread-local descriptor itself. Static TLS storage is still live while
 * POSIX TSD destructors run. */
static void datastack_tls_cleanup(void *value)
{
    stdlib_datastack_tls *ds = &stdlib_tls_datastack;
    (void)value;
    if (ds->base == NULL) return;
    if (ds->backing == 1) {
        if (ds->capacity != 0u) (void)munmap(ds->base, ds->capacity);
    } else {
        /* Heap backing is obtained from the runtime's malloc-backed allocator;
         * plain free() releases the same block without calling runtime code. */
        free(ds->base);
    }
    memset(ds, 0, sizeof *ds);
}

static void datastack_tls_key_init(void)
{
    g_datastack_key_ok =
        (pthread_key_create(&g_datastack_key, datastack_tls_cleanup) == 0);
}

void cplus_datastack_tls_register_thread(void)
{
    (void)pthread_once(&g_datastack_once, datastack_tls_key_init);
    if (!g_datastack_key_ok) return;
    if (pthread_getspecific(g_datastack_key) == NULL)
        (void)pthread_setspecific(g_datastack_key, (void *)&stdlib_tls_datastack);
}

/* The initial thread never passes through cplus_pthread_create(). */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void datastack_tls_main_register(void)
{
    cplus_datastack_tls_register_thread();
}
#endif
#else
void cplus_datastack_tls_register_thread(void) { /* no POSIX TSD on Windows */ }
#endif
