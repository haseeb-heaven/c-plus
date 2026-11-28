/* The header's rich-diagnostics macro must not rewrite this definition.
 * The exported symbol is kept intact and forwards to the location-aware
 * engine with an "unknown" location; in-tree callers use the macro instead. */
#define CPLUS_SAFE_DIAG_NO_MACROS
#include "../safe_log.h"
#include "../safe_diag.h"
#include <stdio.h>
#include <stdatomic.h>

static atomic_int g_level = CPLUS_SAFE_LOG_OFF;

void cplus_safe_log_set_level(cplus_safe_log_level level) {
    /* logger config; no recursive logging */
    atomic_store(&g_level, (int)level);
}
cplus_safe_log_level cplus_safe_log_get_level(void) {
    return (cplus_safe_log_level)atomic_load(&g_level);
}
void cplus_safe_log_at(cplus_safe_log_level level, const char *api,
                       const char *msg, const char *file, int line,
                       const char *func) {
    int cur = atomic_load(&g_level);
    if (cur == CPLUS_SAFE_LOG_OFF || (int)level > cur) return;
    if (!api) api = "(null)";
    if (!msg) msg = "(null)";
    /* Route through the diagnostic engine. LEGACY mode (the default) emits
     * the exact historical one-line text; rich mode adds color, location and
     * an explanation. The level filter above is intentionally preserved: log
     * suppression must behave identically in both modes. */
    cplus_diag_log_at((int)level, api, msg, file, line, func);
}

void cplus_safe_log(cplus_safe_log_level level, const char *api, const char *msg) {
    /* ABI entry point: same filtering, unknown source location. */
    cplus_safe_log_at(level, api, msg, "(cplus_safe_log)", 0, "(external caller)");
}
