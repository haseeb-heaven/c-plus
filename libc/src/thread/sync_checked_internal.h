/* Internal linkage between sync_checked.c and the C11 thread adapter.
 *
 * A DONE slot is a permanent refusal only until the same identity is freshly
 * (re)initialised.  These three hooks let the existing init/create entry
 * points clear that refusal without changing any public signature, struct
 * layout or exported ABI.  Only DONE slots are cleared; a CLAIMED slot is
 * left alone so a concurrent double-destroy can still be refused. */
#ifndef CPLUS_SYNC_CHECKED_INTERNAL_H
#define CPLUS_SYNC_CHECKED_INTERNAL_H

#include "cplus_threads.h"

#ifdef __cplusplus
extern "C" {
#endif

void cplus_sync_checked_rearm_mtx(cplus_mtx_t *mtx);
void cplus_sync_checked_rearm_cnd(cplus_cnd_t *cnd);
void cplus_sync_checked_rearm_thrd(const cplus_thrd_t *thr);

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_SYNC_CHECKED_INTERNAL_H */
