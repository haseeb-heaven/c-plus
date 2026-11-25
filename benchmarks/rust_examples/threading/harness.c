/* C99 harness for the C+ checked thread/synchronization wrappers.
 *
 * Drives the same deterministic cases as threading.rs and prints matching
 * "<api> <case> <token>" lines. Links libcplus_hardened.a (-pthread, -lm as
 * needed for the log/time helpers). Every join is reached through a
 * deterministic handshake; there are no wall-clock assertions.
 *
 * Symbols used (from `nm -g build/libc/libcplus_hardened.a`):
 *   cplus_thrd_create, cplus_thrd_join,
 *   cplus_mtx_init, cplus_mtx_lock, cplus_mtx_unlock, cplus_mtx_trylock,
 *   cplus_mtx_destroy,
 *   cplus_cnd_init, cplus_cnd_wait, cplus_cnd_signal, cplus_cnd_timedwait,
 *   cplus_cnd_destroy,
 *   cplus_tss_create, cplus_tss_get, cplus_tss_set, cplus_tss_delete.
 *
 * Double-destroy refusal is NOT exercised: no checked destroy symbol exists
 * (the two destroy wrappers are `void` and forward directly to pthread, so a
 * second call is undefined behaviour, not a reported refusal). Those lines
 * are emitted as BLOCKED, matching the Rust side. See NOTES.md for nm evidence.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <time.h>

#include "cplus_threads.h"

static void emit(const char *api, const char *case_name, const char *token)
{
    printf("%s %s %s\n", api, case_name, token);
}

/* ------------------------------------------------------- thrd_create / thrd_join */
static int increment_worker(void *arg)
{
    int *p = (int *)arg;
    *p += 1;
    return 42;
}

static void case_spawn_join(void)
{
    cplus_thrd_t t;
    int observed = 0;
    int result = -1;
    int rc = cplus_thrd_create(&t, increment_worker, &observed);
    if (rc != CPLUS_THRD_SUCCESS) {
        emit("thrd_create", "spawn_join", "MISMATCH");
        emit("thrd_join", "result", "MISMATCH");
        return;
    }
    rc = cplus_thrd_join(t, &result);
    emit("thrd_create", "spawn_join",
         (rc == CPLUS_THRD_SUCCESS && observed == 1) ? "OK" : "MISMATCH");
    emit("thrd_join", "result",
         (rc == CPLUS_THRD_SUCCESS && result == 42) ? "OK" : "MISMATCH");
}

/* ---------------------------------------------------------- mtx_lock / mtx_unlock */
static void case_mutex_lock_unlock(void)
{
    cplus_mtx_t mtx;
    int ok = 1;
    if (cplus_mtx_init(&mtx, CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS) {
        emit("mtx_lock", "lock_unlock", "MISMATCH");
        return;
    }
    if (cplus_mtx_lock(&mtx) != CPLUS_THRD_SUCCESS) ok = 0;
    if (cplus_mtx_unlock(&mtx) != CPLUS_THRD_SUCCESS) ok = 0;
    cplus_mtx_destroy(&mtx);
    emit("mtx_lock", "lock_unlock", ok ? "OK" : "MISMATCH");
}

/* --------------------------------------------------------------- mtx_trylock */
static void case_mtx_trylock_busy(void)
{
    cplus_mtx_t mtx;
    int rc;
    if (cplus_mtx_init(&mtx, CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS) {
        emit("mtx_trylock", "busy", "MISMATCH");
        return;
    }
    if (cplus_mtx_lock(&mtx) != CPLUS_THRD_SUCCESS) {
        emit("mtx_trylock", "busy", "MISMATCH");
        (void)cplus_mtx_unlock(&mtx);
        cplus_mtx_destroy(&mtx);
        return;
    }
    /* Held by this plain (non-recursive) mutex: trylock maps EBUSY -> thrd_busy. */
    rc = cplus_mtx_trylock(&mtx);
    (void)cplus_mtx_unlock(&mtx);
    cplus_mtx_destroy(&mtx);
    emit("mtx_trylock", "busy", rc == CPLUS_THRD_BUSY ? "BUSY" : "MISMATCH");
}

/* ------------------------------------------------------- cnd_signal / cnd_wait */
static cplus_mtx_t g_cv_mtx;
static cplus_cnd_t g_cv;
static int g_ready = 0;

static int wait_worker(void *arg)
{
    (void)arg;
    if (cplus_mtx_lock(&g_cv_mtx) != CPLUS_THRD_SUCCESS) return -1;
    while (!g_ready) {
        if (cplus_cnd_wait(&g_cv, &g_cv_mtx) != CPLUS_THRD_SUCCESS) {
            (void)cplus_mtx_unlock(&g_cv_mtx);
            return -1;
        }
    }
    (void)cplus_mtx_unlock(&g_cv_mtx);
    return 0;
}

static void case_condvar_signal_wait(void)
{
    cplus_thrd_t t;
    int result = -1;
    int ok = 1;

    g_ready = 0;
    if (cplus_mtx_init(&g_cv_mtx, CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS ||
        cplus_cnd_init(&g_cv) != CPLUS_THRD_SUCCESS ||
        cplus_thrd_create(&t, wait_worker, NULL) != CPLUS_THRD_SUCCESS) {
        emit("cnd_signal", "signal_wait", "MISMATCH");
        return;
    }
    if (cplus_mtx_lock(&g_cv_mtx) != CPLUS_THRD_SUCCESS) {
        ok = 0;
    } else {
        g_ready = 1;
        if (cplus_cnd_signal(&g_cv) != CPLUS_THRD_SUCCESS) ok = 0;
        (void)cplus_mtx_unlock(&g_cv_mtx);
    }
    if (cplus_thrd_join(t, &result) != CPLUS_THRD_SUCCESS || result != 0) ok = 0;
    cplus_cnd_destroy(&g_cv);
    cplus_mtx_destroy(&g_cv_mtx);
    emit("cnd_signal", "signal_wait", ok ? "OK" : "MISMATCH");
}

/* --------------------------------------------------------------- cnd_timedwait */
static void case_condvar_timedwait(void)
{
    cplus_mtx_t mtx;
    cplus_cnd_t cnd;
    struct timespec past;
    int rc;

    if (cplus_mtx_init(&mtx, CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS ||
        cplus_cnd_init(&cnd) != CPLUS_THRD_SUCCESS) {
        emit("cnd_timedwait", "bounded", "MISMATCH");
        return;
    }
    /* Already-expired absolute deadline: deterministic ETIMEDOUT -> thrd_timedout. */
    if (clock_gettime(CLOCK_REALTIME, &past) != 0) {
        emit("cnd_timedwait", "bounded", "MISMATCH");
        cplus_cnd_destroy(&cnd);
        cplus_mtx_destroy(&mtx);
        return;
    }
    past.tv_sec -= 2;
    if (cplus_mtx_lock(&mtx) != CPLUS_THRD_SUCCESS) {
        emit("cnd_timedwait", "bounded", "MISMATCH");
        cplus_cnd_destroy(&cnd);
        cplus_mtx_destroy(&mtx);
        return;
    }
    rc = cplus_cnd_timedwait(&cnd, &mtx, &past);
    (void)cplus_mtx_unlock(&mtx);
    cplus_cnd_destroy(&cnd);
    cplus_mtx_destroy(&mtx);
    emit("cnd_timedwait", "bounded",
         rc == CPLUS_THRD_TIMEDOUT ? "TIMEDOUT" : "MISMATCH");
}

/* --------------------------------------------------------------- tss_get / tss_set */
static void case_tss(void)
{
    cplus_tss_t key;
    int value = 7;
    void *before;
    void *after;
    int rc;

    if (cplus_tss_create(&key, NULL) != CPLUS_THRD_SUCCESS) {
        emit("tss_get", "unset", "MISMATCH");
        emit("tss_set", "set_get", "MISMATCH");
        return;
    }
    before = cplus_tss_get(key);
    emit("tss_get", "unset", before == NULL ? "NULL" : "MISMATCH");

    rc = cplus_tss_set(key, &value);
    after = cplus_tss_get(key);
    emit("tss_set", "set_get",
         (rc == CPLUS_THRD_SUCCESS && after == &value && *(int *)after == 7)
             ? "OK"
             : "MISMATCH");
    cplus_tss_delete(key);
}

/* ------------------------------------------------------- double-destroy refusal */
static void case_double_destroy(void)
{
    /* No checked destroy symbol exists in the archive (see NOTES.md nm output).
     * The two destroy wrappers are void and forward directly to pthread; a
     * second call would be undefined behaviour, so we deliberately never call
     * it here. Both columns report BLOCKED. */
    emit("mtx_destroy", "double_destroy", "BLOCKED");
    emit("cnd_destroy", "double_destroy", "BLOCKED");
}

int main(void)
{
    case_spawn_join();
    case_mutex_lock_unlock();
    case_mtx_trylock_busy();
    case_condvar_signal_wait();
    case_condvar_timedwait();
    case_tss();
    case_double_destroy();
    return 0;
}
