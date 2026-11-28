/* Platform-backed thread wrappers (C+ libc slice).
 * The host toolchain does NOT provide C11 <threads.h> (verified by compiling a
 * minimal translation unit), so these use POSIX pthreads and are named
 * cplus_pthread_* rather than the C11 cplus_thrd_/cplus_mtx_/cplus_cnd_ names. */
#define _POSIX_C_SOURCE 200809L
#include "../../log/log.h"
#include "../stdlib/datastack_tls_internal.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

/* Every hardened wrapper rejects NULL pointers with EINVAL before touching the
 * host pthread API: several implementations (glibc pthread_mutex_lock(NULL),
 * Apple pthread_cond_wait(NULL, ...)) crash or invoke UB on a NULL object. */

/* The DataStack TLS key needs a non-NULL per-thread value before pthread TSD
 * destructors can run at thread exit.  Register it on the worker's own stack
 * by wrapping the start routine; on allocation failure fall back to the
 * unwrapped call (the thread still runs, only cleanup registration is lost). */
typedef struct cplus_thread_start_ctx_s {
    void *(*start)(void *);
    void *arg;
} cplus_thread_start_ctx;

static void *cplus_thread_start_with_tls(void *p)
{
    cplus_thread_start_ctx ctx = *(cplus_thread_start_ctx *)p;
    free(p);
    cplus_datastack_tls_register_thread();
    return ctx.start(ctx.arg);
}

int cplus_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                         void *(*start)(void *), void *arg)
{
    cplus_thread_start_ctx *ctx;
    int rc;

    cplus_libc_log("pthread_create", "enter");
    if (thread == NULL || start == NULL) return EINVAL;
    ctx = (cplus_thread_start_ctx *)malloc(sizeof *ctx);
    if (ctx == NULL) return pthread_create(thread, attr, start, arg);
    ctx->start = start;
    ctx->arg = arg;
    rc = pthread_create(thread, attr, cplus_thread_start_with_tls, ctx);
    if (rc != 0) free(ctx);
    return rc;
}

int cplus_pthread_join(pthread_t thread, void **retval)
{
    cplus_libc_log("pthread_join", "enter");
    /* A pthread_t is a value type; there is no pointer to validate here. */
    return pthread_join(thread, retval);
}

int cplus_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    cplus_libc_log("pthread_mutex_init", "enter");
    if (mutex == NULL) return EINVAL;
    return pthread_mutex_init(mutex, attr);
}
int cplus_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    cplus_libc_log("pthread_mutex_lock", "enter");
    if (mutex == NULL) return EINVAL;
    return pthread_mutex_lock(mutex);
}
int cplus_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    cplus_libc_log("pthread_mutex_unlock", "enter");
    if (mutex == NULL) return EINVAL;
    return pthread_mutex_unlock(mutex);
}
int cplus_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    cplus_libc_log("pthread_mutex_destroy", "enter");
    if (mutex == NULL) return EINVAL;
    return pthread_mutex_destroy(mutex);
}

int cplus_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    cplus_libc_log("pthread_cond_init", "enter");
    if (cond == NULL) return EINVAL;
    return pthread_cond_init(cond, attr);
}
int cplus_pthread_cond_signal(pthread_cond_t *cond)
{
    cplus_libc_log("pthread_cond_signal", "enter");
    if (cond == NULL) return EINVAL;
    return pthread_cond_signal(cond);
}
int cplus_pthread_cond_broadcast(pthread_cond_t *cond)
{
    cplus_libc_log("pthread_cond_broadcast", "enter");
    if (cond == NULL) return EINVAL;
    return pthread_cond_broadcast(cond);
}
int cplus_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    cplus_libc_log("pthread_cond_wait", "enter");
    if (cond == NULL || mutex == NULL) return EINVAL;
    return pthread_cond_wait(cond, mutex);
}
int cplus_pthread_cond_destroy(pthread_cond_t *cond)
{
    cplus_libc_log("pthread_cond_destroy", "enter");
    if (cond == NULL) return EINVAL;
    return pthread_cond_destroy(cond);
}
