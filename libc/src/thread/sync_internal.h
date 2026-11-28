/* Internal lifecycle hooks between the C11 sync adapters and the additive
 * CHECKED destroy/join guard. Not an installed/exported API: only translation
 * units inside libc/src/thread include this header.
 *
 * A successful init/create at an identity that used to be DONE means the
 * object has been legitimately re-created. The checked guard must forget the
 * old destroy/join record for that identity, otherwise the fresh object would
 * be refused with EINVAL and its OS handle leaked. Failed init/create never
 * calls these hooks; the object was not changed in that case.
 */
#ifndef CPLUS_SYNC_INTERNAL_H
#define CPLUS_SYNC_INTERNAL_H

#include "../../install-include/cplus_threads.h"

#ifdef __cplusplus
extern "C" {
#endif

void cplus_sync_checked_on_mtx_init(const cplus_mtx_t *mtx);
void cplus_sync_checked_on_cnd_init(const cplus_cnd_t *cnd);
void cplus_sync_checked_on_thrd_create(const cplus_thrd_t *thr);

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_SYNC_INTERNAL_H */
