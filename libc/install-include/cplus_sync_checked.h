/* Additive runtime-safety CHECKED wrappers for the C11 synchronization
 * destroy/join APIs in libc/src/thread/threads_c11.c.
 *
 * Problem addressed: cplus_mtx_destroy() and cplus_cnd_destroy() are void
 * forwarders that discard the pthread return code, so a second destroy is
 * undefined behavior with no refusal. cplus_thrd_join() likewise has no
 * guard against a second join of the same handle. These wrappers add NEW
 * symbol names only; the existing declarations, struct layouts, pointer
 * representations and exported ABI are untouched.
 *
 * The guard is a bounded, pthread_mutex-protected side table keyed by the
 * object's address (mutex/cond) or by the entire by-value thread-handle bytes
 * (join). The side table is consulted BEFORE any object pointer is passed to
 * pthread. Each slot carries a monotonic generation token stamped at claim
 * time, so a recycled slot has a fresh (identity, generation) pair.
 */
#ifndef CPLUS_SYNC_CHECKED_H
#define CPLUS_SYNC_CHECKED_H

#include <errno.h> /* EINVAL / ENOMEM / EBUSY / ETIMEDOUT error contract */

#include "cplus_threads.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed capacity of the guard table. A call that would exceed it is refused
 * with ENOMEM; it never dereferences the subject object. */
#define CPLUS_SYNC_CHECKED_CAPACITY 64u

/* Return contract (all three functions):
 *   0       operation performed and underlying call succeeded
 *   EINVAL  NULL / all-zero invalid handle, OR the identity was already
 *           claimed (double destroy / double join) -- refused before the
 *           underlying pthread call, so no UB is triggered
 *   ENOMEM  side table exhausted (no free slot) -- refused before the
 *           underlying pthread call
 *   other   the underlying pthread error code, propagated unchanged
 *           (e.g. EBUSY when destroying a locked mutex); on such a failure
 *           the reserved slot is rolled back so a later retry is possible
 *
 * cplus_thrd_join_checked maps the C11 thrd_* result of cplus_thrd_join to
 * 0 / EBUSY / ENOMEM / ETIMEDOUT / EINVAL (CPLUS_THRD_ERROR -> EINVAL).
 */
int cplus_mtx_destroy_checked(cplus_mtx_t *mtx);
int cplus_cnd_destroy_checked(cplus_cnd_t *cnd);
int cplus_thrd_join_checked(cplus_thrd_t thr, int *res);

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_SYNC_CHECKED_H */
