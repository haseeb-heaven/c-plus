/* Thin ISO C11 <threads.h> ADAPTER over the existing pthread-backed wrappers.
 *
 * This file does NOT implement threading. Where a cplus_pthread_* wrapper
 * already exists in libc/src/thread/thread.c it is reused directly; the
 * remaining entry points are one-line adapters over the corresponding POSIX
 * primitive because this tree ships no wrapper for them (and no local musl
 * threads source was available to copy). All public entry points log an
 * opt-in "enter" marker via cplus_libc_log, matching libc convention.
 *
 * Reused wrappers: create, join, mutex_init/lock/unlock/destroy,
 *                  cond_init/signal/broadcast/wait/destroy, nanosleep.
 * Direct POSIX : pthread_equal/self/detach/exit, sched_yield,
 *                pthread_mutex_trylock, pthread_cond_timedwait,
 *                pthread_key_{create,getspecific,setspecific,delete},
 *                pthread_once.
 *
 * ALL 41 ISO C11 <threads.h> INVENTORY ITEMS ARE ACCOUNTED FOR:
 *   Functions (25), each logged at entry:
 *     thrd_create       -> cplus_pthread_create        (existing wrapper)
 *     thrd_join         -> cplus_pthread_join          (existing wrapper)
 *     thrd_equal        -> pthread_equal               (no wrapper exists)
 *     thrd_current      -> pthread_self                (no wrapper exists)
 *     thrd_sleep        -> cplus_nanosleep             (existing wrapper)
 *     thrd_yield        -> sched_yield                 (no wrapper exists)
 *     thrd_exit         -> pthread_exit                (no wrapper exists)
 *     thrd_detach       -> pthread_detach              (no wrapper exists)
 *     mtx_init          -> cplus_pthread_mutex_init + pthread_mutexattr_*
 *     mtx_lock          -> cplus_pthread_mutex_lock    (existing wrapper)
 *     mtx_unlock        -> cplus_pthread_mutex_unlock  (existing wrapper)
 *     mtx_destroy       -> cplus_pthread_mutex_destroy (existing wrapper)
 *     mtx_trylock       -> pthread_mutex_trylock       (no wrapper exists)
 *     mtx_timedlock     -> trylock + CLOCK_REALTIME nanosleep loop (see below)
 *     cnd_init          -> cplus_pthread_cond_init     (existing wrapper)
 *     cnd_signal        -> cplus_pthread_cond_signal   (existing wrapper)
 *     cnd_broadcast     -> cplus_pthread_cond_broadcast(existing wrapper)
 *     cnd_wait          -> cplus_pthread_cond_wait     (existing wrapper)
 *     cnd_timedwait     -> pthread_cond_timedwait      (no wrapper exists)
 *     cnd_destroy       -> cplus_pthread_cond_destroy  (existing wrapper)
 *     call_once         -> pthread_once                (no wrapper exists)
 *     tss_create        -> pthread_key_create          (no wrapper exists)
 *     tss_get           -> pthread_getspecific         (no wrapper exists)
 *     tss_set           -> pthread_setspecific         (no wrapper exists)
 *     tss_delete        -> pthread_key_delete          (no wrapper exists)
 *   Macros (10): thrd_success/busy/nomem/timedout/error, mtx_plain/
 *     mtx_recursive/mtx_timed, ONCE_FLAG_INIT, TSS_DTOR_ITERATIONS -- all
 *     defined in cplus_threads.h with ISO C11 values; ONCE_FLAG_INIT embeds
 *     the Darwin pthread_once_t ABI signature to stay header-self-contained.
 *   Types (6): thrd_t, thrd_start_t, mtx_t, cnd_t, tss_t, once_flag -- all
 *     defined in cplus_threads.h as opaque, over-sized, aligned storage.
 *   NOTHING IS LEFT MISSING; no behaviour is invented where a POSIX primitive
 *   exists. The only adapted (not direct) case is mtx_timedlock, documented
 *   below, because Darwin does not export pthread_mutex_timedlock().
 *
 * PORTABILITY NOTE: macOS does not provide pthread_mutex_timedlock() nor
 * pthread_mutexattr_setclock(). mtx_timedlock therefore uses the standard
 * trylock + CLOCK_REALTIME deadline/nanosleep fallback (no new sync object).
 * For cnd_timedwait the Darwin condvar clock is already CLOCK_REALTIME, which
 * is the C11-required absolute time base.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "../../log/log.h"
#include "../../install-include/cplus_threads.h"
#include "sync_internal.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Wrappers from thread.c / time_extra.c that this adapter reuses. */
extern int cplus_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
extern int cplus_pthread_join(pthread_t, void **);
extern int cplus_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
extern int cplus_pthread_mutex_lock(pthread_mutex_t *);
extern int cplus_pthread_mutex_unlock(pthread_mutex_t *);
extern int cplus_pthread_mutex_destroy(pthread_mutex_t *);
extern int cplus_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
extern int cplus_pthread_cond_signal(pthread_cond_t *);
extern int cplus_pthread_cond_broadcast(pthread_cond_t *);
extern int cplus_pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
extern int cplus_pthread_cond_destroy(pthread_cond_t *);
extern int cplus_nanosleep(const struct timespec *, struct timespec *);

/* The opaque public storage must be able to hold the real pthread object and
 * be aligned at least as strictly. void* arrays satisfy both on all supported
 * targets; these checks make a future port fail at compile time, not runtime. */
typedef char cplus_c11_thrd_size_check[(sizeof(cplus_thrd_t) >= sizeof(pthread_t)) ? 1 : -1];
typedef char cplus_c11_mtx_size_check[(sizeof(cplus_mtx_t) >= sizeof(pthread_mutex_t)) ? 1 : -1];
typedef char cplus_c11_cnd_size_check[(sizeof(cplus_cnd_t) >= sizeof(pthread_cond_t)) ? 1 : -1];
typedef char cplus_c11_once_size_check[(sizeof(cplus_once_flag) >= sizeof(pthread_once_t)) ? 1 : -1];
typedef char cplus_c11_tss_size_check[(sizeof(cplus_tss_t) >= sizeof(pthread_key_t)) ? 1 : -1];
#if defined(__GNUC__) || defined(__clang__)
typedef char cplus_c11_mtx_align_check[(__alignof__(cplus_mtx_t) >= __alignof__(pthread_mutex_t)) ? 1 : -1];
typedef char cplus_c11_cnd_align_check[(__alignof__(cplus_cnd_t) >= __alignof__(pthread_cond_t)) ? 1 : -1];
typedef char cplus_c11_once_align_check[(__alignof__(cplus_once_flag) >= __alignof__(pthread_once_t)) ? 1 : -1];
#endif

static pthread_mutex_t *pmtx(cplus_mtx_t *m) { return (pthread_mutex_t *)(void *)m; }
static pthread_cond_t *pcnd(cplus_cnd_t *c) { return (pthread_cond_t *)(void *)c; }
static pthread_once_t *ponce(cplus_once_flag *f) { return (pthread_once_t *)(void *)f; }

/* POSIX errno -> ISO C11 thrd_* status code. */
static int status_from_errno(int rc)
{
    if (rc == 0) return CPLUS_THRD_SUCCESS;
    if (rc == EBUSY || rc == EAGAIN) return CPLUS_THRD_BUSY;
    if (rc == ETIMEDOUT) return CPLUS_THRD_TIMEDOUT;
    if (rc == ENOMEM) return CPLUS_THRD_NOMEM;
    return CPLUS_THRD_ERROR;
}

/* C11 start routines return int; pthread start routines return void*. Encode
 * the int in the void* return the same way musl's <threads.h> adapter does.
 *
 * The start context is heap-allocated in cplus_thrd_create and freed by the
 * trampoline before it calls the user's start routine. It must NOT live in the
 * caller's cplus_thrd_t: for a detached thread that handle is an ordinary
 * stack object whose lifetime ends as soon as the creating frame returns, so
 * the worker could otherwise read a clobbered stack frame (use-after-scope).
 * Only the pthread_t is kept in the public handle. */
typedef struct cplus_thrd_ctx_s {
    cplus_thrd_start_t func;
    void *arg;
} cplus_thrd_ctx;

/* Keep pthread_t at the same byte offset as before (two pointers) so the
 * opaque cplus_thrd_t ABI and all of the helpers below are unchanged. */
#define CPLUS_THRD_PT_OFFSET sizeof(cplus_thrd_ctx)
typedef char cplus_c11_thrd_ctx_size_check[
    (sizeof(cplus_thrd_t) >= CPLUS_THRD_PT_OFFSET + sizeof(pthread_t)) ? 1 : -1];

static pthread_t *thr_pt(cplus_thrd_t *t)
{
    return (pthread_t *)(void *)((unsigned char *)t + CPLUS_THRD_PT_OFFSET);
}

static void *cplus_thrd_trampoline(void *p)
{
    cplus_thrd_ctx *ctxp = (cplus_thrd_ctx *)p;
    cplus_thrd_ctx ctx = *ctxp; /* copy func/arg out before releasing the cell */
    free(ctxp);                 /* worker owns the heap context from here on */
    return (void *)(intptr_t)ctx.func(ctx.arg);
}

/* ---------------------------------------------------------------- Threads */
int cplus_thrd_create(cplus_thrd_t *thr, cplus_thrd_start_t func, void *arg)
{
    pthread_t pt;
    cplus_thrd_ctx *ctx;
    int rc;
    cplus_libc_log("thrd_create", "enter");
    if (thr == NULL || func == NULL) return CPLUS_THRD_ERROR;
    memset(thr, 0, sizeof *thr);
    ctx = (cplus_thrd_ctx *)malloc(sizeof *ctx);
    if (ctx == NULL) return CPLUS_THRD_NOMEM;
    ctx->func = func;
    ctx->arg = arg;
    rc = cplus_pthread_create(&pt, NULL, cplus_thrd_trampoline, ctx);
    if (rc != 0) {
        free(ctx); /* pthread_create failed: no worker will claim the context */
        return status_from_errno(rc);
    }
    memcpy(thr_pt(thr), &pt, sizeof pt);
    /* The checked join guard keys on the whole handle; a recycled pthread_t
     * must not be mistaken for a handle whose join was already completed. */
    cplus_sync_checked_on_thrd_create(thr);
    return CPLUS_THRD_SUCCESS;
}

int cplus_thrd_equal(cplus_thrd_t lhs, cplus_thrd_t rhs)
{
    pthread_t a, b;
    cplus_libc_log("thrd_equal", "enter");
    memcpy(&a, (const unsigned char *)&lhs + CPLUS_THRD_PT_OFFSET, sizeof a);
    memcpy(&b, (const unsigned char *)&rhs + CPLUS_THRD_PT_OFFSET, sizeof b);
    return pthread_equal(a, b) != 0;
}

cplus_thrd_t cplus_thrd_current(void)
{
    cplus_thrd_t out;
    pthread_t self;
    cplus_libc_log("thrd_current", "enter");
    memset(&out, 0, sizeof out);
    self = pthread_self();
    memcpy(thr_pt(&out), &self, sizeof self);
    return out;
}

int cplus_thrd_sleep(const struct timespec *duration, struct timespec *remaining)
{
    int rc;
    cplus_libc_log("thrd_sleep", "enter");
    if (duration == NULL) return -2;
    rc = cplus_nanosleep(duration, remaining); /* existing time_extra.c wrapper */
    if (rc == 0) return 0;
    return (errno == EINTR) ? -1 : -2; /* C11: -1 = interrupted, other < 0 = error */
}

void cplus_thrd_yield(void)
{
    cplus_libc_log("thrd_yield", "enter");
    (void)sched_yield();
}

void cplus_thrd_exit(int res)
{
    cplus_libc_log("thrd_exit", "enter");
    pthread_exit((void *)(intptr_t)res);
    for (;;) { (void)sched_yield(); } /* pthread_exit is noreturn; silence analyzers */
}

int cplus_thrd_join(cplus_thrd_t thr, int *res)
{
    pthread_t pt;
    void *rv = NULL;
    int rc;
    cplus_libc_log("thrd_join", "enter");
    memcpy(&pt, thr_pt(&thr), sizeof pt);
    rc = cplus_pthread_join(pt, &rv);
    if (rc != 0) return status_from_errno(rc);
    if (res != NULL) *res = (int)(intptr_t)rv;
    return CPLUS_THRD_SUCCESS;
}

int cplus_thrd_detach(cplus_thrd_t thr)
{
    pthread_t pt;
    cplus_libc_log("thrd_detach", "enter");
    memcpy(&pt, thr_pt(&thr), sizeof pt);
    return status_from_errno(pthread_detach(pt));
}

/* ---------------------------------------------------------------- Mutexes */
int cplus_mtx_init(cplus_mtx_t *mtx, int type)
{
    pthread_mutexattr_t attr;
    int kind, rc;
    cplus_libc_log("mtx_init", "enter");
    if (mtx == NULL) return CPLUS_THRD_ERROR;
    kind = type & ~CPLUS_MTX_TIMED;
    if (kind != CPLUS_MTX_PLAIN && kind != CPLUS_MTX_RECURSIVE) return CPLUS_THRD_ERROR;
    if (pthread_mutexattr_init(&attr) != 0) return CPLUS_THRD_ERROR;
    if (kind == CPLUS_MTX_RECURSIVE) {
        if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0) {
            (void)pthread_mutexattr_destroy(&attr);
            return CPLUS_THRD_ERROR;
        }
    }
    rc = cplus_pthread_mutex_init(pmtx(mtx), &attr); /* existing wrapper */
    (void)pthread_mutexattr_destroy(&attr);
    if (rc == 0) cplus_sync_checked_on_mtx_init(mtx); /* re-arm same address */
    return status_from_errno(rc);
}

int cplus_mtx_lock(cplus_mtx_t *mtx)
{
    cplus_libc_log("mtx_lock", "enter");
    if (mtx == NULL) return CPLUS_THRD_ERROR;
    return status_from_errno(cplus_pthread_mutex_lock(pmtx(mtx)));
}

int cplus_mtx_timedlock(cplus_mtx_t *mtx, const struct timespec *time_point)
{
    cplus_libc_log("mtx_timedlock", "enter");
    if (mtx == NULL || time_point == NULL) return CPLUS_THRD_ERROR;
    /* Darwin has no pthread_mutex_timedlock(): thin trylock/nanosleep loop
     * against a CLOCK_REALTIME absolute deadline (no new sync primitive). */
    for (;;) {
        struct timespec now, delay;
        int rc = pthread_mutex_trylock(pmtx(mtx));
        if (rc == 0) return CPLUS_THRD_SUCCESS;
        if (rc != EBUSY) return status_from_errno(rc);
        if (clock_gettime(CLOCK_REALTIME, &now) != 0) return CPLUS_THRD_ERROR;
        if (now.tv_sec > time_point->tv_sec ||
            (now.tv_sec == time_point->tv_sec && now.tv_nsec >= time_point->tv_nsec))
            return CPLUS_THRD_TIMEDOUT;
        delay.tv_sec = time_point->tv_sec - now.tv_sec;
        delay.tv_nsec = time_point->tv_nsec - now.tv_nsec;
        if (delay.tv_nsec < 0) {
            delay.tv_nsec += 1000000000L;
            delay.tv_sec -= 1;
        }
        (void)nanosleep(&delay, NULL);
    }
}

int cplus_mtx_trylock(cplus_mtx_t *mtx)
{
    cplus_libc_log("mtx_trylock", "enter");
    if (mtx == NULL) return CPLUS_THRD_ERROR;
    return status_from_errno(pthread_mutex_trylock(pmtx(mtx)));
}

int cplus_mtx_unlock(cplus_mtx_t *mtx)
{
    cplus_libc_log("mtx_unlock", "enter");
    if (mtx == NULL) return CPLUS_THRD_ERROR;
    return status_from_errno(cplus_pthread_mutex_unlock(pmtx(mtx)));
}

void cplus_mtx_destroy(cplus_mtx_t *mtx)
{
    cplus_libc_log("mtx_destroy", "enter");
    if (mtx == NULL) return;
    (void)cplus_pthread_mutex_destroy(pmtx(mtx));
}

/* ------------------------------------------------------ Condition variables */
int cplus_cnd_init(cplus_cnd_t *cnd)
{
    int rc;
    cplus_libc_log("cnd_init", "enter");
    if (cnd == NULL) return CPLUS_THRD_ERROR;
    /* NULL attr => CLOCK_REALTIME on every supported platform, as C11 requires. */
    rc = cplus_pthread_cond_init(pcnd(cnd), NULL);
    if (rc == 0) cplus_sync_checked_on_cnd_init(cnd); /* re-arm same address */
    return status_from_errno(rc);
}

int cplus_cnd_signal(cplus_cnd_t *cnd)
{
    cplus_libc_log("cnd_signal", "enter");
    if (cnd == NULL) return CPLUS_THRD_ERROR;
    return status_from_errno(cplus_pthread_cond_signal(pcnd(cnd)));
}

int cplus_cnd_broadcast(cplus_cnd_t *cnd)
{
    cplus_libc_log("cnd_broadcast", "enter");
    if (cnd == NULL) return CPLUS_THRD_ERROR;
    return status_from_errno(cplus_pthread_cond_broadcast(pcnd(cnd)));
}

int cplus_cnd_wait(cplus_cnd_t *cnd, cplus_mtx_t *mtx)
{
    cplus_libc_log("cnd_wait", "enter");
    if (cnd == NULL || mtx == NULL) return CPLUS_THRD_ERROR;
    return status_from_errno(cplus_pthread_cond_wait(pcnd(cnd), pmtx(mtx)));
}

int cplus_cnd_timedwait(cplus_cnd_t *cnd, cplus_mtx_t *mtx, const struct timespec *time_point)
{
    cplus_libc_log("cnd_timedwait", "enter");
    if (cnd == NULL || mtx == NULL || time_point == NULL) return CPLUS_THRD_ERROR;
    return status_from_errno(pthread_cond_timedwait(pcnd(cnd), pmtx(mtx), time_point));
}

void cplus_cnd_destroy(cplus_cnd_t *cnd)
{
    cplus_libc_log("cnd_destroy", "enter");
    if (cnd == NULL) return;
    (void)cplus_pthread_cond_destroy(pcnd(cnd));
}

/* --------------------------------------------------------------- call_once */
void cplus_call_once(cplus_once_flag *flag, void (*func)(void))
{
    cplus_libc_log("call_once", "enter");
    if (flag == NULL || func == NULL) return;
    /* CPLUS_ONCE_FLAG_INIT carries a valid platform PTHREAD_ONCE_INIT image. */
    (void)pthread_once(ponce(flag), func);
}

/* ------------------------------------------------- thread-specific storage */
int cplus_tss_create(cplus_tss_t *key, void (*destructor)(void *))
{
    pthread_key_t pk;
    int rc;
    cplus_libc_log("tss_create", "enter");
    if (key == NULL) return CPLUS_THRD_ERROR;
    rc = pthread_key_create(&pk, destructor);
    if (rc != 0) return status_from_errno(rc);
    *key = (cplus_tss_t)pk;
    return CPLUS_THRD_SUCCESS;
}

void *cplus_tss_get(cplus_tss_t key)
{
    cplus_libc_log("tss_get", "enter");
    return pthread_getspecific((pthread_key_t)key);
}

int cplus_tss_set(cplus_tss_t key, void *val)
{
    cplus_libc_log("tss_set", "enter");
    return status_from_errno(pthread_setspecific((pthread_key_t)key, val));
}

void cplus_tss_delete(cplus_tss_t key)
{
    cplus_libc_log("tss_delete", "enter");
    (void)pthread_key_delete((pthread_key_t)key);
}
