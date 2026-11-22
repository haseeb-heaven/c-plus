/* Opt-in, reentrancy-aware C+ libc logger.
 *
 * Design constraints (see docs/LOGGING.md and docs/libc/C_BUG_CLASS_MATRIX.md):
 *  - OFF by default; a single atomic flag guards entry.
 *  - NO heap allocation and NO stdio buffering: uses write(2) directly. This
 *    makes the logger safe to call from a signal handler (fprintf is NOT
 *    async-signal-safe) and immune to a stale stderr FILE error indicator.
 *    NOTE: async-signal-safety is a design goal, NOT a formally proven
 *    property -- see docs/LOGGING.md for the exact guarantees and caveats.
 *  - Fixed 4096-byte stack buffer: bounded, no unbounded growth. Long api/msg
 *    strings are truncated with a "..." marker rather than copied in full.
 *  - Never changes an API's return value or errno. `errno` is saved on entry
 *    and restored on every exit path, because write(2) sets it on failure.
 *  - Only two fields (api, msg) are logged: no user buffers, file contents or
 *    secrets are ever printed.
 *
 * Write semantics (F6/F7 from docs/libc/AUDIT_lifecycle.md):
 *  - A single write(2) may return a short count, -1/EINTR, or -1/EAGAIN (on a
 *    nonblocking fd). We loop until every byte is written or a hard error or a
 *    bounded retry budget stops us; a partial line is never silently dropped.
 *  - No signal disposition is changed: SIGPIPE keeps the process default, so a
 *    consumer that closes the read end of fd 2 still delivers SIGPIPE (or
 *    EPIPE) exactly as it would for any other write(2) on that descriptor.
 */
#include "log.h"
#include <stdatomic.h>
#include <errno.h>

#if defined(_WIN32)
#include <io.h>
#define CPLUS_LOG_FD 2
typedef int cplus_log_rv;
#define CPLUS_LOG_WRITE(fd, buf, n) _write((fd), (buf), (unsigned)(n))
#define CPLUS_LOG_YIELD() ((void)0)
#else
#include <unistd.h>
#include <sched.h>
#define CPLUS_LOG_FD 2
typedef ssize_t cplus_log_rv;
#define CPLUS_LOG_WRITE(fd, buf, n) write((fd), (buf), (n))
/* On a transient EAGAIN, yield so a concurrent drainer of fd 2 can make
 * progress. sched_yield() is async-signal-safe. */
#define CPLUS_LOG_YIELD() sched_yield()
#endif

#define CPLUS_LOG_CAP 4096u
/* Reserve 4 bytes for the worst-case "...\n" suffix so the truncation path
 * can never write past the stack buffer. */
#define CPLUS_LOG_BODY_CAP (CPLUS_LOG_CAP - 4u)

/* Bound consecutive non-progress retries (EINTR/EAGAIN) so a signal handler
 * can never spin forever on a full/nonblocking fd 2. The counter resets each
 * time bytes are actually written, so a slow drainer cannot trip the bound. */
#define CPLUS_LOG_MAX_RETRIES 100000u

static atomic_int g_enabled = 0;

void cplus_libc_log_set_enabled(int enabled) {
    atomic_store(&g_enabled, enabled ? 1 : 0);
}

static size_t log_append(char *dst, size_t cap, size_t pos, const char *src) {
    if (!src) src = "?";
    while (*src && pos + 1u < cap) {
        dst[pos++] = *src++;
    }
    return pos;
}

void cplus_libc_log(const char *api, const char *msg) {
    int saved_errno = errno;
    char line[CPLUS_LOG_CAP];
    size_t n = 0;
    size_t off = 0;
    unsigned retries = 0;

    if (!atomic_load(&g_enabled)) goto done;

    n = log_append(line, CPLUS_LOG_BODY_CAP, n, "[CPLUS-LIBC] ");
    n = log_append(line, CPLUS_LOG_BODY_CAP, n, api);
    n = log_append(line, CPLUS_LOG_BODY_CAP, n, ": ");
    n = log_append(line, CPLUS_LOG_BODY_CAP, n, msg);
    if (n + 2u <= CPLUS_LOG_BODY_CAP) {
        line[n++] = '\n';
    } else {
        /* Keep the line well-formed even when truncated. */
        line[n++] = '.';
        line[n++] = '.';
        line[n++] = '.';
        line[n++] = '\n';
    }

    /* Emit the whole line. A single write() is NOT guaranteed to consume all
     * of `n` bytes (short write) and may fail transiently with EINTR/EAGAIN. */
    while (off < n) {
        cplus_log_rv w = CPLUS_LOG_WRITE(CPLUS_LOG_FD, line + off, n - off);
        if (w > 0) {
            off += (size_t)w;
            retries = 0; /* progress was made: reset the transient budget */
            continue;
        }
        if (w < 0) {
            int e = errno;
            int retryable = (e == EINTR);
#if !defined(_WIN32)
            if (e == EAGAIN || e == EWOULDBLOCK) retryable = 1;
#endif
            if (retryable && ++retries < CPLUS_LOG_MAX_RETRIES) {
                CPLUS_LOG_YIELD();
                continue;
            }
        }
        /* Hard error (EBADF/EPIPE/ENOSPC/...), zero-byte write, or the retry
         * budget was exhausted: stop without looping forever. */
        break;
    }

done:
    errno = saved_errno;
}
