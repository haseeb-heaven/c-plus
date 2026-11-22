/* Opt-in, reentrancy-aware C+ libc logger.
 *
 * Design constraints (see docs/LOGGING.md and docs/libc/C_BUG_CLASS_MATRIX.md):
 *  - OFF by default; a single atomic flag guards entry.
 *  - NO heap allocation and NO stdio buffering: uses write(2) directly. This
 *    makes the logger safe to call from a signal handler (fprintf is NOT
 *    async-signal-safe) and immune to a stale stderr FILE error indicator.
 *  - Fixed 4096-byte stack buffer: bounded, no unbounded growth. Long api/msg
 *    strings are truncated with a "..." marker rather than copied in full.
 *  - Never changes an API's return value or errno.
 *  - Only two fields (api, msg) are logged: no user buffers, file contents or
 *    secrets are ever printed.
 */
#include "log.h"
#include <stdatomic.h>

#if defined(_WIN32)
#include <io.h>
#define CPLUS_LOG_FD 2
#define CPLUS_LOG_WRITE(fd, buf, n) _write((fd), (buf), (unsigned)(n))
#else
#include <unistd.h>
#define CPLUS_LOG_FD 2
#define CPLUS_LOG_WRITE(fd, buf, n) (void)write((fd), (buf), (n))
#endif

#define CPLUS_LOG_CAP 4096u
/* Reserve 4 bytes for the worst-case "...\n" suffix so the truncation path
 * can never write past the stack buffer. */
#define CPLUS_LOG_BODY_CAP (CPLUS_LOG_CAP - 4u)

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
    char line[CPLUS_LOG_CAP];
    size_t n = 0;
    if (!atomic_load(&g_enabled)) return;

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
    CPLUS_LOG_WRITE(CPLUS_LOG_FD, line, n);
}
