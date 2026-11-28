#ifndef CPLUS_SAFE_STATS_H
#define CPLUS_SAFE_STATS_H
/* C+ safe CRT telemetry: per-API invocation counters.
 *
 * Lightweight, opt-in instrumentation used to observe how each hardened API is
 * actually exercised at runtime (calls / successes / failures). This is
 * diagnostic telemetry, not a safety guarantee: a zero failure count does not
 * prove an API is memory-safe.
 *
 * Thread-safety: counters use C11 atomics; the registry is fixed-size and
 * append-only, so recording from multiple threads is safe.
 */
#include <stddef.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define CPLUS_SAFE_STATS_API_MAX 24

typedef struct cplus_safe_stat {
    char            api[CPLUS_SAFE_STATS_API_MAX];
    unsigned long   calls;
    unsigned long   ok;
    unsigned long   fail;
} cplus_safe_stat;

/* Record one invocation of `api` (NULL/empty is ignored). `ok` != 0 => success. */
void   cplus_safe_stat_record(const char *api, int ok);

/* Zero every counter (keeps registered API names). */
void   cplus_safe_stats_reset(void);

/* Look up counters for `api`; returns 1 and fills *out if found, else 0. */
int    cplus_safe_stats_get(const char *api, cplus_safe_stat *out);

/* Copy up to `max` registered entries into `out`; returns number written. */
size_t cplus_safe_stats_dump(cplus_safe_stat *out, size_t max);

#if defined(__cplusplus)
}
#endif
#endif
