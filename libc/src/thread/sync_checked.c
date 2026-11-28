/* Additive CHECKED wrappers for synchronization destroy/join.
 *
 * libc/src/thread/threads_c11.c exports void cplus_mtx_destroy() and
 * void cplus_cnd_destroy() that discard the pthread return code; a second
 * destroy is therefore UB with no refusal. This translation unit adds three
 * NEW symbols with an int/errno-style contract and a bounded side table that
 * refuses repeat operations. Nothing in the existing ABI or struct layouts
 * changes: the table is keyed by the address bytes of the object (or by the
 * bytes of the by-value thread handle), never by modifying the object.
 *
 * Safety rule: consult metadata first, touch the object second. A slot is
 * reserved under the table lock before the pthread call; a second caller for
 * the same identity is refused with EINVAL without ever dereferencing the
 * stale pointer or re-entering pthread_destroy/pthread_join.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "../../log/log.h"
#include "../../install-include/cplus_threads.h"
#include "../../install-include/cplus_sync_checked.h"
#include "sync_checked_internal.h"

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Existing pthread-level wrappers, defined in libc/src/thread/thread.c and
 * present in build/libc/libcplus_hardened.a. Calling them (rather than the
 * void C11 forwarders) is what lets us report the real pthread error code. */
extern int cplus_pthread_mutex_destroy(pthread_mutex_t *);
extern int cplus_pthread_cond_destroy(pthread_cond_t *);

/* Largest identity stored is a whole by-value cplus_thrd_t. For mutex/cond
 * only pointer-size bytes are used, for join all handle bytes are used (the
 * trailing pthread_t plus the start-context words), which also tolerates
 * pthread_t value recycling across different thread handles. */
#define CPLUS_SYNC_KEY_BYTES sizeof(cplus_thrd_t)
typedef char cplus_sync_key_size_check[(CPLUS_SYNC_KEY_BYTES >= sizeof(void *)) ? 1 : -1];
typedef char cplus_sync_key_len_check[(CPLUS_SYNC_KEY_BYTES <= 255u) ? 1 : -1];

enum { CPLUS_SYNC_KIND_MTX = 1, CPLUS_SYNC_KIND_CND = 2, CPLUS_SYNC_KIND_THRD = 3 };
enum { CPLUS_SYNC_STATE_CLAIMED = 1, CPLUS_SYNC_STATE_DONE = 2 };

typedef struct cplus_sync_slot_s {
    unsigned char key[CPLUS_SYNC_KEY_BYTES];
    unsigned char keylen;   /* 0 == slot unused */
    unsigned char kind;
    unsigned char state;
    uint64_t generation;    /* monotonic per-claim token (slot-reuse ABA guard) */
} cplus_sync_slot;

/* DONE entries are reclaimable: a full table retires the oldest completed
 * identity instead of refusing every later call forever.  A successful
 * init/create at the same address also clears that address's DONE marker. */
static cplus_sync_slot g_slots[CPLUS_SYNC_CHECKED_CAPACITY];
static uint64_t g_generation;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t *pmtx(cplus_mtx_t *m) { return (pthread_mutex_t *)(void *)m; }
static pthread_cond_t *pcnd(cplus_cnd_t *c) { return (pthread_cond_t *)(void *)c; }

static int slot_matches(const cplus_sync_slot *s, int kind, const void *key, unsigned char keylen)
{
    return s->keylen != 0u && s->kind == (unsigned char)kind &&
           s->keylen == keylen && memcmp(s->key, key, (size_t)keylen) == 0;
}

static int all_zero(const void *key, unsigned char keylen)
{
    const unsigned char *p = (const unsigned char *)key;
    unsigned char i;
    for (i = 0u; i < keylen; ++i) {
        if (p[i] != 0u) return 0;
    }
    return 1;
}

/* Reserve an identity or refuse it. Returns 0 with *out set to the reserved
 * slot, EINVAL for a repeat identity, ENOMEM when every slot is still CLAIMED.
 * The subject object is never read here.
 *
 * A DONE slot is *not* a permanent reservation: when the bounded table is
 * full, the oldest DONE identity is retired so later destroy/join calls keep
 * working.  Only a table made up entirely of in-flight (CLAIMED) operations
 * can still refuse with ENOMEM, which is the genuine capacity bound. */
static int claim(int kind, const void *key, unsigned char keylen, int *out)
{
    int i;
    int free_slot = -1;
    int oldest_done = -1;
    uint64_t oldest_generation = 0u;

    (void)pthread_mutex_lock(&g_lock);
    for (i = 0; i < (int)CPLUS_SYNC_CHECKED_CAPACITY; ++i) {
        if (slot_matches(&g_slots[i], kind, key, keylen)) {
            (void)pthread_mutex_unlock(&g_lock);
            return EINVAL; /* already destroyed / joined or operation in flight */
        }
    }
    for (i = 0; i < (int)CPLUS_SYNC_CHECKED_CAPACITY; ++i) {
        if (g_slots[i].keylen == 0u) { free_slot = i; break; }
    }
    if (free_slot < 0) {
        /* Recycle the least-recently claimed completed operation. The scan
         * above has already proven that no slot matches this identity. */
        for (i = 0; i < (int)CPLUS_SYNC_CHECKED_CAPACITY; ++i) {
            if (g_slots[i].keylen != 0u &&
                g_slots[i].state == CPLUS_SYNC_STATE_DONE &&
                (oldest_done < 0 || g_slots[i].generation < oldest_generation)) {
                oldest_done = i;
                oldest_generation = g_slots[i].generation;
            }
        }
        free_slot = oldest_done;
    }
    if (free_slot < 0) {
        (void)pthread_mutex_unlock(&g_lock);
        return ENOMEM; /* all slots are in-flight: bounded table refusal */
    }

    /* Generation token: bumped for every claim, so a slot recycled after a
     * rolled-back operation carries a distinct (identity, generation) pair.
     * Only the reserved slot is written; the public object is untouched. */
    ++g_generation;
    if (g_generation == 0u) ++g_generation; /* never hand out generation 0 */

    memset(&g_slots[free_slot], 0, sizeof g_slots[free_slot]);
    memcpy(g_slots[free_slot].key, key, (size_t)keylen);
    g_slots[free_slot].keylen = keylen;
    g_slots[free_slot].kind = (unsigned char)kind;
    g_slots[free_slot].state = CPLUS_SYNC_STATE_CLAIMED;
    g_slots[free_slot].generation = g_generation;
    *out = free_slot;
    (void)pthread_mutex_unlock(&g_lock);
    return 0;
}

/* Mark a reserved slot DONE on success, or release it on underlying failure
 * so that a legitimately retried operation is not permanently blocked. */
static void finish(int slot, int ok)
{
    (void)pthread_mutex_lock(&g_lock);
    if (ok) {
        g_slots[slot].state = CPLUS_SYNC_STATE_DONE;
    } else {
        memset(&g_slots[slot], 0, sizeof g_slots[slot]);
    }
    (void)pthread_mutex_unlock(&g_lock);
}

/* A freshly initialised object at a recycled address must be destroyable
 * again.  Clear only DONE identities: a still-CLAIMED slot represents an
 * operation that is in flight and must remain a refusal. */
static void rearm(int kind, const void *key, unsigned char keylen)
{
    int i;
    (void)pthread_mutex_lock(&g_lock);
    for (i = 0; i < (int)CPLUS_SYNC_CHECKED_CAPACITY; ++i) {
        if (g_slots[i].state == CPLUS_SYNC_STATE_DONE &&
            slot_matches(&g_slots[i], kind, key, keylen)) {
            memset(&g_slots[i], 0, sizeof g_slots[i]);
        }
    }
    (void)pthread_mutex_unlock(&g_lock);
}

void cplus_sync_checked_rearm_mtx(cplus_mtx_t *mtx)
{
    if (mtx != NULL) rearm(CPLUS_SYNC_KIND_MTX, &mtx, (unsigned char)sizeof mtx);
}

void cplus_sync_checked_rearm_cnd(cplus_cnd_t *cnd)
{
    if (cnd != NULL) rearm(CPLUS_SYNC_KIND_CND, &cnd, (unsigned char)sizeof cnd);
}

void cplus_sync_checked_rearm_thrd(const cplus_thrd_t *thr)
{
    if (thr != NULL) rearm(CPLUS_SYNC_KIND_THRD, thr, (unsigned char)sizeof *thr);
}

int cplus_mtx_destroy_checked(cplus_mtx_t *mtx)
{
    unsigned char key[CPLUS_SYNC_KEY_BYTES];
    int slot;
    int rc;

    cplus_libc_log("mtx_destroy_checked", "enter");
    if (mtx == NULL) return EINVAL;

    memset(key, 0, sizeof key);
    memcpy(key, &mtx, sizeof mtx); /* pointer VALUE only; never dereferenced */
    rc = claim(CPLUS_SYNC_KIND_MTX, key, (unsigned char)sizeof mtx, &slot);
    if (rc != 0) return rc;

    rc = cplus_pthread_mutex_destroy(pmtx(mtx));
    finish(slot, rc == 0);
    return rc; /* 0, or the raw pthread error (e.g. EBUSY) */
}

int cplus_cnd_destroy_checked(cplus_cnd_t *cnd)
{
    unsigned char key[CPLUS_SYNC_KEY_BYTES];
    int slot;
    int rc;

    cplus_libc_log("cnd_destroy_checked", "enter");
    if (cnd == NULL) return EINVAL;

    memset(key, 0, sizeof key);
    memcpy(key, &cnd, sizeof cnd); /* pointer VALUE only; never dereferenced */
    rc = claim(CPLUS_SYNC_KIND_CND, key, (unsigned char)sizeof cnd, &slot);
    if (rc != 0) return rc;

    rc = cplus_pthread_cond_destroy(pcnd(cnd));
    finish(slot, rc == 0);
    return rc;
}

int cplus_thrd_join_checked(cplus_thrd_t thr, int *res)
{
    unsigned char key[CPLUS_SYNC_KEY_BYTES];
    int slot;
    int rc;

    cplus_libc_log("thrd_join_checked", "enter");
    memset(key, 0, sizeof key);
    memcpy(key, &thr, sizeof thr); /* by-value handle bytes, no dereference */
    if (all_zero(key, (unsigned char)sizeof thr)) return EINVAL; /* NULL-equivalent handle */

    rc = claim(CPLUS_SYNC_KIND_THRD, key, (unsigned char)sizeof thr, &slot);
    if (rc != 0) return rc;

    rc = cplus_thrd_join(thr, res);
    finish(slot, rc == CPLUS_THRD_SUCCESS);
    switch (rc) {
    case CPLUS_THRD_SUCCESS:  return 0;
    case CPLUS_THRD_BUSY:     return EBUSY;
    case CPLUS_THRD_NOMEM:    return ENOMEM;
    case CPLUS_THRD_TIMEDOUT: return ETIMEDOUT;
    default:                  return EINVAL; /* CPLUS_THRD_ERROR */
    }
}
