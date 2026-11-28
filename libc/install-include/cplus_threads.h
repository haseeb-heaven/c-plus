/* C+ namespaced ISO C11 <threads.h> surface.
 *
 * The build host does NOT provide <threads.h> (compile probe: "threads.h file
 * not found"), so this header is SELF-CONTAINED: it never includes <threads.h>
 * and never exposes a pthread_* type. The pthread types live only inside
 * libc/src/thread/threads_c11.c. The public handle types below are opaque,
 * over-sized, pointer-aligned byte storage; the implementation file carries
 * compile-time size/alignment checks against the real pthread objects.
 *
 * This is a thin ADAPTER over the already-shipped pthread-backed
 * cplus_pthread_* wrappers in libc/src/thread/thread.c -- not a new threading
 * implementation. Each function emits an opt-in cplus_libc_log entry marker.
 *
 * Namespaced spelling (cplus_thrd_*, cplus_mtx_*, cplus_cnd_*, cplus_tss_*,
 * cplus_call_once) is the project convention. The canonical ISO C11 spellings
 * (thrd_create, mtx_t, ONCE_FLAG_INIT, ...) are provided below as source-level
 * aliases so C11-style code compiles against this header unchanged.
 */
#ifndef CPLUS_THREADS_H
#define CPLUS_THREADS_H

#include <stddef.h>
#include <stdint.h> /* uintptr_t: Darwin ONCE_FLAG_INIT ABI signature */
#include <time.h>   /* struct timespec */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- thrd_* return codes (ISO C11 values) ------------------------------ */
enum {
    CPLUS_THRD_SUCCESS  = 0,
    CPLUS_THRD_BUSY     = 1,
    CPLUS_THRD_NOMEM    = 2,
    CPLUS_THRD_TIMEDOUT = 3,
    CPLUS_THRD_ERROR    = 4
};

/* ---- mtx_init type flags ------------------------------------------------ */
#define CPLUS_MTX_PLAIN     0
#define CPLUS_MTX_RECURSIVE 1
#define CPLUS_MTX_TIMED     2

#define CPLUS_TSS_DTOR_ITERATIONS 4

/* ---- Opaque handle storage ----------------------------------------------
 * void* arrays give pointer alignment (sufficient for every pthread object)
 * and generous room: Darwin pthread_mutex_t=64B, pthread_cond_t=48B,
 * pthread_once_t=16B, pthread_t=8B, pthread_key_t=8B. The thread handle also
 * holds the start context (two pointers) plus the pthread_t. The .c asserts
 * all size/alignment requirements.
 */
typedef struct cplus_thrd_s { void *__cplus_opaque[4]; }  cplus_thrd_t;
typedef struct cplus_mtx_s  { void *__cplus_opaque[16]; } cplus_mtx_t;
typedef struct cplus_cnd_s  { void *__cplus_opaque[16]; } cplus_cnd_t;
typedef struct cplus_once_s { void *__cplus_opaque[8]; }  cplus_once_flag;
typedef unsigned long cplus_tss_t;
typedef int (*cplus_thrd_start_t)(void *);

#if defined(__APPLE__)
/* Darwin pthread_once_t is { long __sig; char __opaque[8]; } and
 * PTHREAD_ONCE_INIT stores _PTHREAD_ONCE_SIG_init (0x30B1BCBA, from
 * pthread/pthread_impl.h) in __sig. A zeroed object is NOT a valid initializer
 * on this platform, so the ABI signature is spelled out here to keep the
 * header self-contained. Other platforms use all-zero PTHREAD_ONCE_INIT. */
#define CPLUS_ONCE_FLAG_INIT { { (void *)(uintptr_t)0x30B1BCBAu } }
#else
#define CPLUS_ONCE_FLAG_INIT { { 0 } }
#endif

/* ---- Threads ------------------------------------------------------------ */
int          cplus_thrd_create(cplus_thrd_t *thr, cplus_thrd_start_t func, void *arg);
int          cplus_thrd_equal(cplus_thrd_t lhs, cplus_thrd_t rhs);
cplus_thrd_t cplus_thrd_current(void);
int          cplus_thrd_sleep(const struct timespec *duration, struct timespec *remaining);
void         cplus_thrd_yield(void);
void         cplus_thrd_exit(int res);
int          cplus_thrd_join(cplus_thrd_t thr, int *res);
int          cplus_thrd_detach(cplus_thrd_t thr);

/* ---- Mutexes ------------------------------------------------------------ */
int  cplus_mtx_init(cplus_mtx_t *mtx, int type);
int  cplus_mtx_lock(cplus_mtx_t *mtx);
int  cplus_mtx_timedlock(cplus_mtx_t *mtx, const struct timespec *time_point);
int  cplus_mtx_trylock(cplus_mtx_t *mtx);
int  cplus_mtx_unlock(cplus_mtx_t *mtx);
void cplus_mtx_destroy(cplus_mtx_t *mtx);

/* ---- Condition variables ------------------------------------------------ */
int  cplus_cnd_init(cplus_cnd_t *cnd);
int  cplus_cnd_signal(cplus_cnd_t *cnd);
int  cplus_cnd_broadcast(cplus_cnd_t *cnd);
int  cplus_cnd_wait(cplus_cnd_t *cnd, cplus_mtx_t *mtx);
int  cplus_cnd_timedwait(cplus_cnd_t *cnd, cplus_mtx_t *mtx, const struct timespec *time_point);
void cplus_cnd_destroy(cplus_cnd_t *cnd);

/* ---- Call once / thread-specific storage -------------------------------- */
void  cplus_call_once(cplus_once_flag *flag, void (*func)(void));
int   cplus_tss_create(cplus_tss_t *key, void (*destructor)(void *));
void *cplus_tss_get(cplus_tss_t key);
int   cplus_tss_set(cplus_tss_t key, void *val);
void  cplus_tss_delete(cplus_tss_t key);

/* ---- Canonical ISO C11 spellings (source compatibility) ----------------- */
#define thrd_create    cplus_thrd_create
#define thrd_equal     cplus_thrd_equal
#define thrd_current   cplus_thrd_current
#define thrd_sleep     cplus_thrd_sleep
#define thrd_yield     cplus_thrd_yield
#define thrd_exit      cplus_thrd_exit
#define thrd_join      cplus_thrd_join
#define thrd_detach    cplus_thrd_detach
#define mtx_init       cplus_mtx_init
#define mtx_lock       cplus_mtx_lock
#define mtx_timedlock  cplus_mtx_timedlock
#define mtx_trylock    cplus_mtx_trylock
#define mtx_unlock     cplus_mtx_unlock
#define mtx_destroy    cplus_mtx_destroy
#define cnd_init       cplus_cnd_init
#define cnd_signal     cplus_cnd_signal
#define cnd_broadcast  cplus_cnd_broadcast
#define cnd_wait       cplus_cnd_wait
#define cnd_timedwait  cplus_cnd_timedwait
#define cnd_destroy    cplus_cnd_destroy
#define call_once      cplus_call_once
#define tss_create     cplus_tss_create
#define tss_get        cplus_tss_get
#define tss_set        cplus_tss_set
#define tss_delete     cplus_tss_delete

#define thrd_t         cplus_thrd_t
#define thrd_start_t   cplus_thrd_start_t
#define mtx_t          cplus_mtx_t
#define cnd_t          cplus_cnd_t
#define tss_t          cplus_tss_t
#define once_flag      cplus_once_flag

#define thrd_success   CPLUS_THRD_SUCCESS
#define thrd_busy      CPLUS_THRD_BUSY
#define thrd_nomem     CPLUS_THRD_NOMEM
#define thrd_timedout  CPLUS_THRD_TIMEDOUT
#define thrd_error     CPLUS_THRD_ERROR
#define mtx_plain      CPLUS_MTX_PLAIN
#define mtx_recursive  CPLUS_MTX_RECURSIVE
#define mtx_timed      CPLUS_MTX_TIMED
#define ONCE_FLAG_INIT CPLUS_ONCE_FLAG_INIT
#define TSS_DTOR_ITERATIONS CPLUS_TSS_DTOR_ITERATIONS

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_THREADS_H */
