#define _POSIX_C_SOURCE 200112L
#include "../safe_stats.h"
#include "../safe_log.h"
#include <pthread.h>
#include <string.h>

/* Fixed registry of safe CRT APIs.  Counters use compiler atomic builtins.
 * Registration is race-free: slots are published with a release store of the
 * slot count after the name is written, readers scan lock-free up to an
 * acquire load of the count, and only registration takes the mutex. */
typedef struct slot {
    char            api[CPLUS_SAFE_STATS_API_MAX];
    unsigned long   calls;
    unsigned long   ok;
    unsigned long   fail;
} slot;

#define CPLUS_SAFE_STATS_CAP 64
static slot g_slots[CPLUS_SAFE_STATS_CAP];
static int  g_count = 0;
static pthread_mutex_t g_reg_lock = PTHREAD_MUTEX_INITIALIZER;

static int scan_slots(const char *api, int n) {
    for (int i = 0; i < n; ++i)
        if (strncmp(g_slots[i].api, api, CPLUS_SAFE_STATS_API_MAX - 1) == 0) return i;
    return -1;
}

static int find_slot(const char *api) {
    return scan_slots(api, __atomic_load_n(&g_count, __ATOMIC_ACQUIRE));
}

static int register_slot(const char *api) {
    int idx;
    pthread_mutex_lock(&g_reg_lock);
    idx = scan_slots(api, g_count);          /* another thread may have won */
    if (idx < 0 && g_count < CPLUS_SAFE_STATS_CAP) {
        idx = g_count;
        strncpy(g_slots[idx].api, api, CPLUS_SAFE_STATS_API_MAX - 1);
        g_slots[idx].api[CPLUS_SAFE_STATS_API_MAX - 1] = '\0';
        __atomic_store_n(&g_count, idx + 1, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&g_reg_lock);
    return idx;
}

void cplus_safe_stat_record(const char *api, int ok) {
    int idx;
    if (!api || !api[0]) return;
    idx = find_slot(api);
    if (idx < 0) idx = register_slot(api);
    if (idx < 0) return;
    __atomic_fetch_add(&g_slots[idx].calls, 1UL, __ATOMIC_RELAXED);
    if (ok) __atomic_fetch_add(&g_slots[idx].ok, 1UL, __ATOMIC_RELAXED);
    else    __atomic_fetch_add(&g_slots[idx].fail, 1UL, __ATOMIC_RELAXED);
}

void cplus_safe_stats_reset(void) {
    int n = __atomic_load_n(&g_count, __ATOMIC_ACQUIRE);
    for (int i = 0; i < n; ++i) {
        __atomic_store_n(&g_slots[i].calls, 0UL, __ATOMIC_RELAXED);
        __atomic_store_n(&g_slots[i].ok, 0UL, __ATOMIC_RELAXED);
        __atomic_store_n(&g_slots[i].fail, 0UL, __ATOMIC_RELAXED);
    }
}

int cplus_safe_stats_get(const char *api, cplus_safe_stat *out) {
    int idx;
    if (!api || !out) return 0;
    idx = find_slot(api);
    if (idx < 0) return 0;
    strncpy(out->api, g_slots[idx].api, CPLUS_SAFE_STATS_API_MAX - 1);
    out->api[CPLUS_SAFE_STATS_API_MAX - 1] = '\0';
    out->calls = __atomic_load_n(&g_slots[idx].calls, __ATOMIC_RELAXED);
    out->ok    = __atomic_load_n(&g_slots[idx].ok, __ATOMIC_RELAXED);
    out->fail  = __atomic_load_n(&g_slots[idx].fail, __ATOMIC_RELAXED);
    return 1;
}

size_t cplus_safe_stats_dump(cplus_safe_stat *out, size_t max) {
    size_t w = 0;
    int n = __atomic_load_n(&g_count, __ATOMIC_ACQUIRE);
    if (!out) return 0;
    for (int i = 0; i < n && w < max; ++i) {
        strncpy(out[w].api, g_slots[i].api, CPLUS_SAFE_STATS_API_MAX - 1);
        out[w].api[CPLUS_SAFE_STATS_API_MAX - 1] = '\0';
        out[w].calls = __atomic_load_n(&g_slots[i].calls, __ATOMIC_RELAXED);
        out[w].ok    = __atomic_load_n(&g_slots[i].ok, __ATOMIC_RELAXED);
        out[w].fail  = __atomic_load_n(&g_slots[i].fail, __ATOMIC_RELAXED);
        ++w;
    }
    return w;
}
