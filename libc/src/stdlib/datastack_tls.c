/* Single definition of the thread-local scratch stack for the modern C+
 * DataStack allocator.  Kept dependency-free so it builds with plain clang in
 * the hardened-libc build; stdlib/datastack.hp declares the matching extern.
 * The field order/types must mirror stdlib_datastack in that header.
 *
 * __thread alone has no destructor: every worker thread that ever allocated a
 * scratch stack leaked its backing memory permanently at thread exit. The C++
 * thread-exit hook (__cxa_thread_atexit / _impl) runs a destructor while the
 * calling thread's TLS is still intact and is armed here from the C+ thread
 * creation wrappers. A pthread-key destructor is kept as a fallback on hosts
 * without the C++ hook. The TLS struct layout/ABI is unchanged.
 */
#define _POSIX_C_SOURCE 200809L
#include "datastack_tls_internal.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/mman.h>
#endif

__thread stdlib_datastack_tls stdlib_tls_datastack;

static __thread int g_datastack_registered;
static unsigned long g_datastack_reclaims;

/* Weak declarations: a host without the C++ thread-exit hook falls back to
 * the pthread-key path instead of failing to link. */
typedef int (*cplus_tls_atexit_fn)(void (*)(void *), void *, void *);
#if defined(__APPLE__)
extern int __cxa_thread_atexit(void (*)(void *), void *, void *);
extern void *__dso_handle;
#else
extern int __cxa_thread_atexit(void (*)(void *), void *, void *)
    __attribute__((weak));
extern int __cxa_thread_atexit_impl(void (*)(void *), void *, void *)
    __attribute__((weak));
extern void *__dso_handle __attribute__((weak));
#endif

static void datastack_tls_destructor(void *value)
{
    stdlib_datastack_tls *ds = (stdlib_datastack_tls *)value;
    if (ds == NULL) return;
    if (ds->base == NULL) return;
    if (ds->backing == 1) {
#if !defined(_WIN32)
        if (ds->capacity != 0u) (void)munmap(ds->base, ds->capacity);
#endif
    } else {
        /* Heap fallback was obtained from cplus_alloc(), which registers the
         * block in the runtime live ledger. Release through cplus_release()
         * when the runtime is linked so the ledger entry is removed; plain-C
         * consumers without the runtime keep the host-free fallback. */
#if defined(__GNUC__) || defined(__clang__)
#  if defined(__APPLE__)
        /* Mach-O shared libraries reject plain undefined weak references;
         * weak_import is the Darwin spelling for an optional dyld symbol. */
        extern void cplus_release(void *) __attribute__((weak_import));
#  else
        extern void cplus_release(void *) __attribute__((weak));
#  endif
        if (cplus_release != NULL)
            cplus_release(ds->base);
        else
            free(ds->base);
#else
        free(ds->base);
#endif
    }
    (void)__atomic_fetch_add(&g_datastack_reclaims, 1u, __ATOMIC_RELAXED);
    memset(ds, 0, sizeof *ds); /* also invalidates every outstanding mark */
}

/* pthread-key fallback used only when no C++ thread-exit hook is available. */
static pthread_key_t g_datastack_key;
static pthread_once_t g_datastack_once = PTHREAD_ONCE_INIT;
static int g_datastack_key_ready;

static void fallback_destructor(void *value)
{
    (void)value;
    datastack_tls_destructor(&stdlib_tls_datastack);
}

static void create_datastack_key(void)
{
    if (pthread_key_create(&g_datastack_key, fallback_destructor) == 0)
        g_datastack_key_ready = 1;
}

int cplus_datastack_tls_thread_register(void)
{
    cplus_tls_atexit_fn fn = NULL;
    if (g_datastack_registered) return 0;
    if ((cplus_tls_atexit_fn)__cxa_thread_atexit != NULL) fn = __cxa_thread_atexit;
#if !defined(__APPLE__)
    if (fn == NULL && (cplus_tls_atexit_fn)__cxa_thread_atexit_impl != NULL)
        fn = __cxa_thread_atexit_impl;
#endif
    if (fn != NULL) {
        int rc = fn(datastack_tls_destructor, &stdlib_tls_datastack,
                    (void *)&__dso_handle);
        if (rc == 0) { g_datastack_registered = 1; return 0; }
    }
    (void)pthread_once(&g_datastack_once, create_datastack_key);
    if (!g_datastack_key_ready) return -1;
    if (pthread_setspecific(g_datastack_key, &stdlib_tls_datastack) != 0) return -1;
    g_datastack_registered = 1;
    return 0;
}

unsigned long cplus_datastack_tls_reclaim_count(void)
{
    return (unsigned long)__atomic_load_n(&g_datastack_reclaims, __ATOMIC_RELAXED);
}

/* The initial thread may also use datastack without going through a wrapper. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void register_initial_thread(void)
{
    (void)cplus_datastack_tls_thread_register();
}
#endif
