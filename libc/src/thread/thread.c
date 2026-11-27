/* Platform-backed thread wrappers (C+ libc slice).
 * The host toolchain does NOT provide C11 <threads.h> (verified by compiling a
 * minimal translation unit), so these use POSIX pthreads and are named
 * cplus_pthread_* rather than the C11 cplus_thrd_/cplus_mtx_/cplus_cnd_ names.
 *
 * Required pointer arguments are validated before forwarding: host pthread_*
 * implementations are not required to tolerate NULL and several dereference
 * it immediately (SIGSEGV/UB). Optional arguments (attr, retval) keep their
 * POSIX meaning and may be NULL.
 */
#define _POSIX_C_SOURCE 200809L
#include "../../log/log.h"
#include "../stdlib/datastack_tls_internal.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

/* Worker entry: arm the DataStack TLS exit destructor before user code runs,
 * so threads created through the C+ wrappers cannot leak their scratch stack. */
typedef struct cplus_thread_start_s {
    void *(*start)(void *);
    void *arg;
} cplus_thread_start;

static void *cplus_thread_entry(void *p)
{
    cplus_thread_start ctx = *(cplus_thread_start *)p;
    free(p);
    (void)cplus_datastack_tls_thread_register();
    return ctx.start(ctx.arg);
}

int cplus_pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*start)(void *),void *arg){
    cplus_thread_start *ctx;
    int rc;
    cplus_libc_log("pthread_create","enter");
    if(thread==NULL||start==NULL){errno=EINVAL;return EINVAL;}
    ctx = (cplus_thread_start *)malloc(sizeof *ctx);
    if(ctx==NULL){errno=EAGAIN;return EAGAIN;}
    ctx->start = start; ctx->arg = arg;
    rc = pthread_create(thread,attr,cplus_thread_entry,ctx);
    if(rc!=0) free(ctx);
    return rc;
}
int cplus_pthread_join(pthread_t thread,void **retval){ cplus_libc_log("pthread_join","enter"); return pthread_join(thread,retval); }

int cplus_pthread_mutex_init(pthread_mutex_t *mutex,const pthread_mutexattr_t *attr){ cplus_libc_log("pthread_mutex_init","enter"); if(mutex==NULL){errno=EINVAL;return EINVAL;} return pthread_mutex_init(mutex,attr); }
int cplus_pthread_mutex_lock(pthread_mutex_t *mutex){ cplus_libc_log("pthread_mutex_lock","enter"); if(mutex==NULL){errno=EINVAL;return EINVAL;} return pthread_mutex_lock(mutex); }
int cplus_pthread_mutex_unlock(pthread_mutex_t *mutex){ cplus_libc_log("pthread_mutex_unlock","enter"); if(mutex==NULL){errno=EINVAL;return EINVAL;} return pthread_mutex_unlock(mutex); }
int cplus_pthread_mutex_destroy(pthread_mutex_t *mutex){ cplus_libc_log("pthread_mutex_destroy","enter"); if(mutex==NULL){errno=EINVAL;return EINVAL;} return pthread_mutex_destroy(mutex); }

int cplus_pthread_cond_init(pthread_cond_t *cond,const pthread_condattr_t *attr){ cplus_libc_log("pthread_cond_init","enter"); if(cond==NULL){errno=EINVAL;return EINVAL;} return pthread_cond_init(cond,attr); }
int cplus_pthread_cond_signal(pthread_cond_t *cond){ cplus_libc_log("pthread_cond_signal","enter"); if(cond==NULL){errno=EINVAL;return EINVAL;} return pthread_cond_signal(cond); }
int cplus_pthread_cond_broadcast(pthread_cond_t *cond){ cplus_libc_log("pthread_cond_broadcast","enter"); if(cond==NULL){errno=EINVAL;return EINVAL;} return pthread_cond_broadcast(cond); }
int cplus_pthread_cond_wait(pthread_cond_t *cond,pthread_mutex_t *mutex){ cplus_libc_log("pthread_cond_wait","enter"); if(cond==NULL||mutex==NULL){errno=EINVAL;return EINVAL;} return pthread_cond_wait(cond,mutex); }
int cplus_pthread_cond_destroy(pthread_cond_t *cond){ cplus_libc_log("pthread_cond_destroy","enter"); if(cond==NULL){errno=EINVAL;return EINVAL;} return pthread_cond_destroy(cond); }
