#include "log.h"
#include <stdatomic.h>
#include <stdio.h>
static atomic_int g_enabled = 0;
void cplus_libc_log_set_enabled(int enabled) { atomic_store(&g_enabled, enabled ? 1 : 0); }
void cplus_libc_log(const char *api, const char *msg) {
    if (!atomic_load(&g_enabled)) return;
    if (!api) api = "?";
    if (!msg) msg = "?";
    fprintf(stderr, "[CPLUS-LIBC] %s: %s\n", api, msg);
}
