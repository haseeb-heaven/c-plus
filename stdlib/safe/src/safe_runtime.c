#include "../safe_runtime.h"
#include "../safe_log.h"
#include "../safe_stats.h"
#include <setjmp.h>
#include <stdio.h>
#include <unistd.h>

#if defined(_MSC_VER)
#  define CPLUS_TLS __declspec(thread)
#else
#  define CPLUS_TLS _Thread_local
#endif

static CPLUS_TLS jmp_buf  g_guard;
static CPLUS_TLS int      g_guard_active;
static CPLUS_TLS char     g_last_message[256];
static cplus_runtime_handler g_handler;

void cplus_runtime_set_handler(cplus_runtime_handler handler) {
    g_handler = handler;
    cplus_safe_log(CPLUS_SAFE_LOG_INFO, "runtime", "handler set");
    cplus_safe_stat_record("runtime_set_handler", 1);
}

void cplus_runtime_report(const char *message) {
    if (!message) message = "runtime error";
    snprintf(g_last_message, sizeof g_last_message, "%s", message);
    cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "runtime", message);
    cplus_safe_stat_record("runtime_report", 0);
    if (g_handler) g_handler(message);
    /* Always emit a diagnostic so the error is never silent. */
    fprintf(stderr, "[CPLUS-RUNTIME] runtime error: %s\n", message);
    fflush(stderr);
}

int cplus_runtime_guard(int (*body)(void *ctx), void *ctx) {
    if (!body) return CPLUS_RUNTIME_ERROR;
    if (setjmp(g_guard) != 0) {
        g_guard_active = 0;
        return CPLUS_RUNTIME_ERROR;   /* unwound from a reported runtime error */
    }
    g_guard_active = 1;
    int rc = body(ctx);
    g_guard_active = 0;
    return rc == 0 ? CPLUS_RUNTIME_OK : CPLUS_RUNTIME_ERROR;
}

/* UBSan recover-mode hook: called by the sanitizer runtime on each report.
 * When inside a guard, unwind to it instead of letting execution continue. */
void __ubsan_on_report(void);
void __ubsan_on_report(void) {
    cplus_runtime_report("undefined behavior detected by C+ runtime (UBSan)");
    if (g_guard_active) {
        g_guard_active = 0;
        longjmp(g_guard, 1);          /* recover: unwind to the guard */
    }
    /* No guard: never crash with a signal and never continue silently past
     * undefined behavior. Terminate cleanly with a defined exit code. */
    fflush(NULL);
    _exit(70);                        /* EX_SOFTWARE-style controlled failure */
}
