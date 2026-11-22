/* Platform-backed thread wrappers (C+ libc slice).
 * The host toolchain does NOT provide C11 <threads.h> (verified by compiling a
 * minimal translation unit), so these use POSIX pthreads and are named
 * cplus_pthread_* rather than the C11 cplus_thrd_/cplus_mtx_/cplus_cnd_ names. */
#define _POSIX_C_SOURCE 200809L
#include "../../log/log.h"
#include <pthread.h>

int cplus_pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*start)(void *),void *arg){ cplus_libc_log("pthread_create","enter"); return pthread_create(thread,attr,start,arg); }
int cplus_pthread_join(pthread_t thread,void **retval){ cplus_libc_log("pthread_join","enter"); return pthread_join(thread,retval); }

int cplus_pthread_mutex_init(pthread_mutex_t *mutex,const pthread_mutexattr_t *attr){ cplus_libc_log("pthread_mutex_init","enter"); return pthread_mutex_init(mutex,attr); }
int cplus_pthread_mutex_lock(pthread_mutex_t *mutex){ cplus_libc_log("pthread_mutex_lock","enter"); return pthread_mutex_lock(mutex); }
int cplus_pthread_mutex_unlock(pthread_mutex_t *mutex){ cplus_libc_log("pthread_mutex_unlock","enter"); return pthread_mutex_unlock(mutex); }
int cplus_pthread_mutex_destroy(pthread_mutex_t *mutex){ cplus_libc_log("pthread_mutex_destroy","enter"); return pthread_mutex_destroy(mutex); }

int cplus_pthread_cond_init(pthread_cond_t *cond,const pthread_condattr_t *attr){ cplus_libc_log("pthread_cond_init","enter"); return pthread_cond_init(cond,attr); }
int cplus_pthread_cond_signal(pthread_cond_t *cond){ cplus_libc_log("pthread_cond_signal","enter"); return pthread_cond_signal(cond); }
int cplus_pthread_cond_broadcast(pthread_cond_t *cond){ cplus_libc_log("pthread_cond_broadcast","enter"); return pthread_cond_broadcast(cond); }
int cplus_pthread_cond_wait(pthread_cond_t *cond,pthread_mutex_t *mutex){ cplus_libc_log("pthread_cond_wait","enter"); return pthread_cond_wait(cond,mutex); }
int cplus_pthread_cond_destroy(pthread_cond_t *cond){ cplus_libc_log("pthread_cond_destroy","enter"); return pthread_cond_destroy(cond); }
