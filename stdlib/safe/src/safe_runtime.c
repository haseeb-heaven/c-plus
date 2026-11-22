#define _POSIX_C_SOURCE 200112L
#include "../safe_runtime.h"
#include "../safe_log.h"
#include "../safe_stats.h"
#include <setjmp.h>
#include <stdio.h>
#include <unistd.h>

/* Thread-local storage keyword without requiring C11 under -std=c99. */
#if defined(_MSC_VER)
#  define CPLUS_TLS __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#  define CPLUS_TLS __thread
#else
#  define CPLUS_TLS _Thread_local
#endif

typedef struct guard_frame {
    jmp_buf jb;
    struct guard_frame *prev;
    int base;                       /* cleanup depth at guard entry */
} guard_frame;

typedef struct cleanup_entry {
    void (*drop)(void *);
    void *self;
} cleanup_entry;

static CPLUS_TLS guard_frame *g_top;
static CPLUS_TLS cleanup_entry g_cleanups[CPLUS_RUNTIME_CLEANUP_MAX];
static CPLUS_TLS int g_ncleanups;
static CPLUS_TLS unsigned long g_leaked;
static CPLUS_TLS unsigned long g_unwound;
static cplus_runtime_handler g_handler;
static volatile int g_failstop;
static volatile int g_violation_trap = 1;

void cplus_runtime_set_handler(cplus_runtime_handler handler) {
    g_handler = handler;
    cplus_safe_log(CPLUS_SAFE_LOG_INFO, "runtime", "handler set");
    cplus_safe_stat_record("runtime_set_handler", 1);
}

void cplus_runtime_set_failstop(int enabled) { g_failstop = enabled ? 1 : 0; }
int cplus_runtime_get_failstop(void) { return g_failstop; }
void cplus_runtime_set_violation_trap(int enabled) { g_violation_trap = enabled ? 1 : 0; }
int cplus_runtime_get_violation_trap(void) { return g_violation_trap; }

void cplus_runtime_violation(const char *api, const char *detail) {
    const char *a = api ? api : "runtime";
    const char *d = detail ? detail : "memory safety violation";
    fprintf(stderr, "[CPLUS-SAFE] %s: %s\n", a, d);
    fflush(stderr);
    if (g_violation_trap) {
        if (g_top && !g_failstop) {
            longjmp(g_top->jb, 1);
        }
        fflush(NULL);
        _exit(70);
    }
}

void cplus_runtime_report(const char *message) {
    if (!message) message = "runtime error";
    cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "runtime", message);
    cplus_safe_stat_record("runtime_report", 0);
    if (g_handler) g_handler(message);
    /* Always emit a diagnostic so the error is never silent. The message is
     * written verbatim and never interpreted as a format string. */
    fprintf(stderr, "[CPLUS-RUNTIME] runtime error: %s\n", message);
    fflush(stderr);
}

int cplus_runtime_push_cleanup(void (*drop)(void *), void *self) {
    if (!drop || g_ncleanups >= CPLUS_RUNTIME_CLEANUP_MAX) {
        cplus_safe_stat_record("runtime_push_clean", 0);
        return -1;
    }
    g_cleanups[g_ncleanups].drop = drop;
    g_cleanups[g_ncleanups].self = self;
    ++g_ncleanups;
    return 0;
}

void cplus_runtime_pop_cleanup(int execute) {
    cleanup_entry e;
    if (g_ncleanups <= 0) return;
    e = g_cleanups[--g_ncleanups];
    g_cleanups[g_ncleanups].drop = NULL;
    g_cleanups[g_ncleanups].self = NULL;
    if (execute && e.drop) e.drop(e.self);
}

int cplus_runtime_cleanup_mark(void) { return g_ncleanups; }
int cplus_runtime_cleanup_depth(void) { return g_ncleanups; }

/* Runs entries above `mark` newest-first.  Each entry is removed BEFORE its
 * drop runs, so a drop that itself fails/unwinds can never run twice. */
void cplus_runtime_cleanup_unwind_to(int mark) {
    if (mark < 0) mark = 0;
    while (g_ncleanups > mark) {
        cleanup_entry e = g_cleanups[--g_ncleanups];
        g_cleanups[g_ncleanups].drop = NULL;
        g_cleanups[g_ncleanups].self = NULL;
        ++g_unwound;
        if (e.drop) e.drop(e.self);
    }
}

void cplus_runtime_unwind_cleanups(void) { cplus_runtime_cleanup_unwind_to(0); }
unsigned long cplus_runtime_leaked_cleanups(void) { return g_leaked; }
unsigned long cplus_runtime_unwound_cleanups(void) { return g_unwound; }
void cplus_runtime_reset_counters(void) { g_leaked = 0; g_unwound = 0; }

int cplus_runtime_guard(int (*body)(void *ctx), void *ctx) {
    guard_frame frame;
    int rc;
    if (!body) return CPLUS_RUNTIME_ERROR;
    frame.prev = g_top;
    frame.base = g_ncleanups;
    if (setjmp(frame.jb) != 0) {
        g_top = frame.prev;
        cplus_runtime_cleanup_unwind_to(frame.base);
        return CPLUS_RUNTIME_ERROR;   /* unwound from a reported runtime error */
    }
    g_top = &frame;
    rc = body(ctx);
    g_top = frame.prev;
    if (g_ncleanups > frame.base) {
        g_leaked += (unsigned long)(g_ncleanups - frame.base);
        cplus_runtime_cleanup_unwind_to(frame.base);
    }
    return rc == 0 ? CPLUS_RUNTIME_OK : CPLUS_RUNTIME_ERROR;
}

/* UBSan recover-mode hook: called by the sanitizer runtime on each report.
 * Inside a guard (and not in fail-stop mode) unwind to it instead of letting
 * execution continue into the faulting operation. */
void __ubsan_on_report(void);
void __ubsan_on_report(void) {
    cplus_runtime_report("undefined behavior detected by C+ runtime (UBSan)");
    if (g_top && !g_failstop) {
        longjmp(g_top->jb, 1);        /* recover: unwind to the innermost guard */
    }
    /* No guard (or fail-stop): never crash with a signal and never continue
     * silently past undefined behavior. Terminate with a defined exit code. */
    fflush(NULL);
    _exit(70);                        /* EX_SOFTWARE-style controlled failure */
}
