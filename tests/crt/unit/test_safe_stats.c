/* Telemetry: per-API invocation counters for the safe CRT. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_io.h"
#include "safe_format.h"
#include "safe_error.h"
#include "safe_stats.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_stats_reset();

    char d[16], s[8] = "abc";
    /* Generate a known mix of success/failure calls. */
    assert(cplus_memcpy_s(d, sizeof d, s, 3) == 0);
    assert(cplus_memcpy_s(d, sizeof d, s, 3) == 0);
    assert(cplus_memcpy_s(d, 2, s, 4) != 0);
    for (int i = 0; i < 7; ++i) assert(cplus_memset_s(d, sizeof d, 0, sizeof d) == 0);

    cplus_safe_stat st;
    assert(cplus_safe_stats_get("memcpy_s", &st) == 1);
    assert(st.calls == 3);
    assert(st.ok == 2);
    assert(st.fail == 1);
    assert(cplus_safe_stats_get("memset_s", &st) == 1);
    assert(st.calls == 7 && st.ok == 7 && st.fail == 0);

    /* unknown API -> not found */
    assert(cplus_safe_stats_get("no_such_api", &st) == 0);

    /* alloc family counted */
    void *p = malloc_s(32);
    assert(p != NULL);
    free_s(p);
    assert(cplus_safe_stats_get("malloc_s", &st) == 1 && st.calls == 1 && st.ok == 1);
    assert(cplus_safe_stats_get("free_s", &st) == 1 && st.calls == 1);

    /* format family counted */
    char b[16]; assert(cplus_snprintf_s(b, sizeof b, "%d", 5) == 1);
    assert(cplus_safe_stats_get("snprintf_s", &st) == 1 && st.calls == 1);

    /* reset zeroes counters but keeps entries */
    cplus_safe_stats_reset();
    assert(cplus_safe_stats_get("memcpy_s", &st) == 1);
    assert(st.calls == 0 && st.ok == 0 && st.fail == 0);

    /* dump returns registered entries */
    cplus_safe_stat out[64];
    size_t n = cplus_safe_stats_dump(out, 64);
    assert(n >= 4);

    /* invalid record args are ignored (no crash) */
    cplus_safe_stat_record(NULL, 1);
    cplus_safe_stat_record("", 1);
    cplus_safe_stat_record("x", 1);

    printf("safe_stats: ok (%zu apis tracked)\n", n);
    return 0;
}
