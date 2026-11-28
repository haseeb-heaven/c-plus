#define _POSIX_C_SOURCE 200112L
/* Keep the exported ABI definitions below intact; the header's rich-diagnostic
 * macros are for callers, not for the definitions themselves. */
#define CPLUS_SAFE_DIAG_NO_MACROS
#include "../safe_runtime.h"
#include "../safe_diag.h"
#include "../safe_log.h"
#include "../safe_stats.h"
#include <setjmp.h>
#include <stdint.h>
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
    unsigned long long token;       /* generation of this activation */
    unsigned long long prev_token;  /* token to restore on exit */
} guard_frame;

typedef struct cleanup_entry {
    void (*drop)(void *);
    void *self;
} cleanup_entry;

static CPLUS_TLS cleanup_entry g_cleanups[CPLUS_RUNTIME_CLEANUP_MAX];
static CPLUS_TLS int g_ncleanups;
static CPLUS_TLS unsigned long g_leaked;
static CPLUS_TLS unsigned long g_unwound;
/* Process-wide control state.  `volatile` gave no inter-thread ordering; the
 * setters may run on one thread while another is reporting a violation.  Use
 * the compiler's atomic builtins (available under -std=c99) so reads and
 * writes are ordered and race-free without changing the exported ABI. */
static cplus_runtime_handler g_handler;
static int g_failstop;
static int g_violation_trap = 1;
static CPLUS_TLS unsigned long long g_guard_gen;
static CPLUS_TLS guard_frame *g_top;

void cplus_runtime_set_handler(cplus_runtime_handler handler) {
    __atomic_store_n(&g_handler, handler, __ATOMIC_RELEASE);
    cplus_safe_log(CPLUS_SAFE_LOG_INFO, "runtime", "handler set");
    cplus_safe_stat_record("runtime_set_handler", 1);
}

void cplus_runtime_set_failstop(int enabled) {
    __atomic_store_n(&g_failstop, enabled ? 1 : 0, __ATOMIC_RELEASE);
}
int cplus_runtime_get_failstop(void) { return __atomic_load_n(&g_failstop, __ATOMIC_ACQUIRE); }
void cplus_runtime_set_violation_trap(int enabled) {
    __atomic_store_n(&g_violation_trap, enabled ? 1 : 0, __ATOMIC_RELEASE);
}
int cplus_runtime_get_violation_trap(void) { return __atomic_load_n(&g_violation_trap, __ATOMIC_ACQUIRE); }

/* Is g_top a guard activation that is still on THIS thread's stack?  A body
 * that leaves through an outer longjmp/exception bypasses the restore in
 * cplus_runtime_guard(), leaving g_top pointing into a dead stack frame.
 * Jumping through its jmp_buf afterwards is UB / control-flow hijack, so the
 * generation and stack ancestry are both checked before every longjmp. */
static guard_frame *live_guard_frame(void) {
    guard_frame *f = g_top;
    uintptr_t sp, fa;
    if (f == NULL || f->token == 0u || f->token != g_guard_gen) return NULL;
#if defined(__GNUC__) || defined(__clang__)
    sp = (uintptr_t)__builtin_frame_address(0);
#else
    { int mark; sp = (uintptr_t)&mark; }
#endif
    fa = (uintptr_t)f;
    /* On a downward-growing stack an active ancestor frame has a HIGHER
     * address than the current frame.  After an outer unwind the current
     * frame is above the abandoned guard, so fa < sp and we fail closed. */
    if (fa < sp) return NULL;
    if (fa - sp > (uintptr_t)(64u << 20)) return NULL; /* implausibly distant */
    return f;
}

void cplus_runtime_violation_at(const char *api, const char *detail,
                                const char *file, int line, const char *func) {
    const char *a = api ? api : "runtime";
    const char *d = detail ? detail : "memory safety violation";
    /* Emit the diagnostic (LEGACY keeps the exact historical line; rich mode
     * adds color/code/location/explanation), then apply the SAME trap policy
     * as before: unwind to a guard or fail stop with exit 70. */
    cplus_diag_violation_at(a, d, file, line, func);
    if (__atomic_load_n(&g_violation_trap, __ATOMIC_ACQUIRE)) {
        guard_frame *f = live_guard_frame();
        if (f != NULL && !__atomic_load_n(&g_failstop, __ATOMIC_ACQUIRE))
            longjmp(f->jb, 1);
        fflush(NULL);
        _exit(70);
    }
}

void cplus_runtime_violation(const char *api, const char *detail) {
    /* ABI entry point: full behaviour, unknown source location. */
    cplus_runtime_violation_at(api, detail, "(cplus_runtime_violation)", 0,
                               "(external caller)");
}

void cplus_runtime_report_at(const char *message, const char *file, int line,
                             const char *func) {
    if (!message) message = "runtime error";
    cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "runtime", message);
    cplus_safe_stat_record("runtime_report", 0);
    {
        /* Load once atomically: a concurrent setter must not change the
         * callee between the NULL test and the call (TOCTOU). */
        cplus_runtime_handler handler = __atomic_load_n(&g_handler, __ATOMIC_ACQUIRE);
        if (handler) handler(message);
    }
    /* Always emit a diagnostic so the error is never silent. The message is
     * written verbatim and never interpreted as a format string. */
    cplus_diag_report_at(message, file, line, func);
}

void cplus_runtime_report(const char *message) {
    /* ABI entry point: full behaviour, unknown source location. */
    cplus_runtime_report_at(message, "(cplus_runtime_report)", 0,
                            "(external caller)");
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
    /* Volatile copies survive a longjmp back into this frame even if the
     * guard's locals would otherwise be indeterminate after the jump. */
    {
        volatile guard_frame *volatile prev_frame = g_top;
        volatile unsigned long long prev_token = g_guard_gen;
        frame.prev = (guard_frame *)prev_frame;
        frame.prev_token = (unsigned long long)prev_token;
        frame.base = g_ncleanups;
        frame.token = ++g_guard_gen;
        if (setjmp(frame.jb) != 0) {
            g_top = (guard_frame *)prev_frame;
            g_guard_gen = prev_token;
            cplus_runtime_cleanup_unwind_to(frame.base);
            return CPLUS_RUNTIME_ERROR; /* unwound from a reported runtime error */
        }
    }
    g_top = &frame;
    rc = body(ctx);
    g_top = frame.prev;
    g_guard_gen = frame.prev_token;
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
    {
        guard_frame *f = live_guard_frame();
        if (f != NULL && !__atomic_load_n(&g_failstop, __ATOMIC_ACQUIRE))
            longjmp(f->jb, 1);        /* recover: unwind to a live guard */
    }
    /* No guard (or fail-stop): never crash with a signal and never continue
     * silently past undefined behavior. Terminate with a defined exit code. */
    fflush(NULL);
    _exit(70);                        /* EX_SOFTWARE-style controlled failure */
}
