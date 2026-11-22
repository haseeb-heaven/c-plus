#ifndef CPLUS_SAFE_RUNTIME_H
#define CPLUS_SAFE_RUNTIME_H
/* C+ recoverable runtime-error handling (Agent 2 library surface).
 *
 * Purpose: when a program is built with recoverable sanitizers
 * (`-fsanitize-recover=all`), a detected runtime error (out-of-bounds, null,
 * misalignment, signed overflow) is routed to a handler that reports it and
 * unwinds to a guard, instead of aborting the process.
 *
 * IMPORTANT: this does NOT make the erroneous operation safe, and it MUST NOT be
 * used to silently continue past memory corruption. The guard should stop the
 * offending work and return a defined error / terminate cleanly with a message.
 */
#include <stddef.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define CPLUS_RUNTIME_OK      0
#define CPLUS_RUNTIME_ERROR   1

typedef void (*cplus_runtime_handler)(const char *message);

/* Install a process/thread handler for runtime errors (report-only hooks). */
void cplus_runtime_set_handler(cplus_runtime_handler handler);

/* Report a runtime error (called by the UBSan hook and by library code). */
void cplus_runtime_report(const char *message);

/* Run `body(ctx)` under a recovery guard. Returns CPLUS_RUNTIME_OK if the body
 * completed, or CPLUS_RUNTIME_ERROR if a runtime error was detected and the
 * body was unwound. The error is always reported; it is never silent. */
int cplus_runtime_guard(int (*body)(void *ctx), void *ctx);

#if defined(__cplusplus)
}
#endif
#endif
