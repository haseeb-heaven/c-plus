#include "../safe_log.h"
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
void cplus_safe_log(cplus_safe_log_level level, const char *api, const char *msg) {
    int cur = atomic_load(&g_level);
    if (cur == CPLUS_SAFE_LOG_OFF || (int)level > cur) return;
    if (!api) api = "(null)";
    if (!msg) msg = "(null)";
    /* metadata only, no buffers */
    fprintf(stderr, "[SAFECRT][%d] %s: %s\n", (int)level, api, msg);
}
