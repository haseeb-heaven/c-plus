/* Safe variant of cve_like/case_15.c -- reset/free vs worker data race.
 *
 * Default build:  C+ SafeCRT layer  (stdlib/safe/safe_c.h)
 * -DPLAIN_LIBC:   plain hardened libc layer (cplus_libc_safe.h)
 *
 * WHY THIS IS NOT FIXABLE BY LIBC
 * -------------------------------
 * The original reads and writes the global `active` pointer in a busy worker
 * with no synchronization while main does `active = NULL; free(o)`. That is a
 * data race on `active` and can become a use-after-free on the pointee. No
 * libc wrapper can serialize two threads' access to a caller-owned pointer.
 *
 * FIX (caller-side): one pthread_mutex guards every read/write of `active`
 * AND the dereference of the pointed-to block. The worker never keeps a copy
 * of `active` across unlock, so it can never dereference a pointer main has
 * freed. A `stop` flag terminates the worker so the thread is joined instead
 * of being killed at process exit.
 */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#endif
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

static uint64_t *active = NULL;
static int stop = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&lock);
        if (stop) {
            pthread_mutex_unlock(&lock);
            break;
        }
        uint64_t *p = active;
        if (p) *p += 1; /* dereference only while holding the lock */
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

int main(void) {
    pthread_t t;
    int cmd = 0;
    int resets = 0;

    active = calloc(1024, sizeof(uint64_t));
    if (!active) {
        printf("OUTCOME: allocation failed (safe)\n");
        return 2;
    }
    if (pthread_create(&t, NULL, worker, NULL) != 0) {
        free(active);
        active = NULL;
        printf("OUTCOME: pthread_create failed (safe)\n");
        return 2;
    }

    printf("1 to reset (anything else / EOF to stop): ");
    while (scanf("%d", &cmd) == 1) {
        if (cmd != 1) break;
        pthread_mutex_lock(&lock);
        uint64_t *old = active;
        active = NULL; /* publish NULL before freeing, under the same lock */
        free(old);     /* worker cannot be holding `old` now */
        pthread_mutex_unlock(&lock);
        resets++;
        printf("reset %d done\n", resets);
    }

    /* Ask the worker to exit and wait for it before final cleanup. */
    pthread_mutex_lock(&lock);
    stop = 1;
    pthread_mutex_unlock(&lock);
    pthread_join(t, NULL);

    pthread_mutex_lock(&lock);
    uint64_t *remaining = active;
    active = NULL;
    pthread_mutex_unlock(&lock);
    free(remaining);

    printf("OUTCOME: thread joined, no reset/free race, resets=%d (safe)\n", resets);
    return 0;
}
