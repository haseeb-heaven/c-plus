/*===-- violation_log.c - bounded runtime-violation diagnostic log --*- C -*-===
 *
 * Additive, opt-in diagnostic logging for runtime safety events.  This module
 * does NOT change the existing fatal/trap ABI: cplus_fatal(),
 * cplus_check_bounds(), cplus_check_nonnull(), cplus_runtime_violation() and
 * cplus_runtime_get_{violation_trap,failstop}() keep their exact behaviour.
 * Callers may additionally route a description of a violation into this log.
 *
 * Design constraints:
 *  - No call-site path or hotkey is hard-coded: the caller supplies `path` to
 *    cplus_violation_log_open().  Nothing is opened until that call succeeds.
 *  - Refuses to log when the log is not open (returns an errno code).
 *  - Fixed-format lines only.  `kind` is never used as a format string; it is
 *    copied with a bounded length and the whole line is built with one
 *    snprintf() into a fixed stack buffer.  No unbounded formatting, no heap.
 *  - Atomic append: one open(O_APPEND) file descriptor plus one bounded
 *    write() per event, guarded by a process-wide mutex.  A single short
 *    write() of a line < PIPE_BUF is atomic at the OS level for regular files.
 *  - Thread-safe: every state transition and the count are protected by one
 *    pthread mutex.  A canary / out-of-bounds probe cannot be touched because
 *    the implementation never writes to caller memory.
 *
 *===----------------------------------------------------------------------===*/

#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#endif

#include "cplus_violation_log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
#define VIO_LOCK() pthread_mutex_lock(&g_lock)
#define VIO_UNLOCK() pthread_mutex_unlock(&g_lock)
#else
/* The supported build is POSIX (macOS/Linux with -pthread).  The no-op lock
 * keeps the translation unit compilable on Windows where these APIs are inert.
 */
#define VIO_LOCK() ((void)0)
#define VIO_UNLOCK() ((void)0)
#endif

/* Bounded formatting: the kind string is truncated to VIO_KIND_CAP characters
 * and the entire rendered line is capped by VIO_LINE_CAP.  Both are far below
 * PIPE_BUF (4096 on POSIX), so one write() is an atomic append. */
#define VIO_KIND_CAP 96u
#define VIO_LINE_CAP 256u

/* Upper bound on the number of events a process may log.  Prevents an
 * unbounded diagnostic log from exhausting disk during a violation storm. */
#define VIO_MAX_EVENTS 1048576UL

static int g_fd = -1;              /* -1 => log is closed */
static unsigned long g_count = 0UL; /* cumulative successful events */

static void vio_close_locked(void) {
#ifndef _WIN32
    if (g_fd >= 0) {
        /* Raw write() is unbuffered: there is nothing to flush, close() is the
         * flush point for durability of the O_APPEND writes already issued. */
        (void)close(g_fd);
        g_fd = -1;
    }
#else
    g_fd = -1;
#endif
}

int cplus_violation_log_open(const char *path) {
    int rc = 0;
#ifndef _WIN32
    int fd;
#endif

    VIO_LOCK();
    if (path == NULL || path[0] == '\0') {
        VIO_UNLOCK();
        return EINVAL;
    }
    /* Re-opening replaces the previous destination; do not leak its fd. */
    vio_close_locked();
#ifndef _WIN32
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        rc = errno;
        if (rc == 0)
            rc = EIO;
        VIO_UNLOCK();
        return rc;
    }
    g_fd = fd;
#else
    (void)path;
    rc = EIO; /* no file backend on the non-POSIX fallback */
#endif
    VIO_UNLOCK();
    return rc;
}

int cplus_violation_log_event(int code, const char *kind, const void *ptr,
                              size_t size) {
    char line[VIO_LINE_CAP];
    int written;
    size_t n;
    size_t off = 0;
    int rc = 0;

    VIO_LOCK();

    if (kind == NULL || kind[0] == '\0') {
        VIO_UNLOCK();
        return EINVAL;
    }
    if (g_fd < 0) {
        VIO_UNLOCK();
        return EBADF; /* refuse when the log is not open */
    }
    if (g_count >= VIO_MAX_EVENTS) {
        VIO_UNLOCK();
        return EFBIG; /* bounded log: cap total events */
    }

    /* Fixed format.  `kind` is data, never a format string.  The precision
     * argument bounds how many bytes of `kind` may be read/copied, and
     * snprintf() bounds the total by sizeof(line). */
    written = snprintf(line, sizeof(line), "%d %.*s 0x%llx %zu\n", code,
                       (int)VIO_KIND_CAP, kind,
                       (unsigned long long)(uintptr_t)ptr, size);
    if (written < 0) {
        VIO_UNLOCK();
        return EIO;
    }
    n = (size_t)written;
    if (n > sizeof(line))
        n = sizeof(line); /* defensive: never write past the stack buffer */

#ifndef _WIN32
    while (off < n) {
        ssize_t w = write(g_fd, line + off, n - off);
        if (w > 0) {
            off += (size_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR)
            continue; /* bounded by progress; a hard error exits below */
        rc = (w < 0 && errno != 0) ? errno : EIO;
        break;
    }
    if (off == n) {
        ++g_count; /* count only fully written lines */
        rc = 0;
    }
#else
    (void)off;
    (void)n;
    rc = EIO;
#endif

    VIO_UNLOCK();
    return rc;
}

void cplus_violation_log_close(void) {
    VIO_LOCK();
    vio_close_locked();
    VIO_UNLOCK();
}

unsigned long cplus_violation_log_count(void) {
    unsigned long c;
    VIO_LOCK();
    c = g_count;
    VIO_UNLOCK();
    return c;
}
