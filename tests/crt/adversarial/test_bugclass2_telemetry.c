/*
 * Bug CLASS: missing/incorrect telemetry on the hardened API surface.
 *
 * The checked C+ safe CRT exposes per-API counters (calls / ok / fail). This
 * test proves record/get/dump agree for several APIs, that record() ignores
 * NULL/empty names, and that a reset is clean (counters zeroed, registrations
 * kept) so telemetry cannot be silently stale between runs.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_alloc.h"
#include "safe_stats.h"
#include "safe_error.h"
#include "safe_log.h"

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_stats_reset();

    cplus_safe_stat st;
    cplus_safe_stat snap[64];

    /* --- record a known ok/fail mix on a private API name --- */
    cplus_safe_stat_record("h2_alpha", 1);
    cplus_safe_stat_record("h2_alpha", 1);
    cplus_safe_stat_record("h2_alpha", 1);
    cplus_safe_stat_record("h2_alpha", 0);
    cplus_safe_stat_record("h2_alpha", 0);
    CHECK(cplus_safe_stats_get("h2_alpha", &st) == 1);
    CHECK(st.calls == 5);
    CHECK(st.ok == 3);
    CHECK(st.fail == 2);
    CHECK(strcmp(st.api, "h2_alpha") == 0);

    cplus_safe_stat_record("h2_beta", 0);
    CHECK(cplus_safe_stats_get("h2_beta", &st) == 1);
    CHECK(st.calls == 1);
    CHECK(st.ok == 0);
    CHECK(st.fail == 1);

    /* --- NULL / empty API names are ignored: no new entry, no crash --- */
    {
        size_t before = cplus_safe_stats_dump(snap, 64);
        cplus_safe_stat_record(NULL, 1);
        cplus_safe_stat_record("", 1);
        CHECK(cplus_safe_stats_dump(snap, 64) == before);
        CHECK(cplus_safe_stats_get("h2_alpha", &st) == 1 && st.calls == 5);
    }

    /* --- a real hardened API is counted by its own name, ok and fail --- */
    {
        char d[16];
        CHECK(cplus_memcpy_s(d, sizeof d, "abc", 3) == 0);      /* ok   */
        CHECK(cplus_memcpy_s(d, 2, "abcd", 4) != 0);            /* fail */
    }
    CHECK(cplus_safe_stats_get("memcpy_s", &st) == 1);
    CHECK(st.calls == 2);
    CHECK(st.ok == 1);
    CHECK(st.fail == 1);

    {
        void *p = NULL;
        CHECK(malloc_s(&p, 64) == CPLUS_SAFE_OK);
        free_s(&p);
    }
    CHECK(cplus_safe_stats_get("malloc_s", &st) == 1 && st.calls == 1 && st.ok == 1);
    CHECK(cplus_safe_stats_get("free_s", &st) == 1 && st.calls == 1 && st.ok == 1);

    /* --- unknown API -> not found, nothing fabricated --- */
    CHECK(cplus_safe_stats_get("no_such_api_h2", &st) == 0);

    /* --- dump lists every registered API we drove --- */
    {
        size_t n = cplus_safe_stats_dump(snap, 64);
        CHECK(n >= 5);
        CHECK(n <= 64);
    }

    /* --- reset is clean: counters zeroed, registrations preserved --- */
    cplus_safe_stats_reset();
    CHECK(cplus_safe_stats_get("h2_alpha", &st) == 1);
    CHECK(st.calls == 0 && st.ok == 0 && st.fail == 0);
    CHECK(cplus_safe_stats_get("memcpy_s", &st) == 1);
    CHECK(st.calls == 0 && st.ok == 0 && st.fail == 0);
    CHECK(cplus_safe_stats_get("malloc_s", &st) == 1);
    CHECK(st.calls == 0 && st.ok == 0 && st.fail == 0);
    {
        size_t n1 = cplus_safe_stats_dump(snap, 64);
        size_t n2 = cplus_safe_stats_dump(snap, 64);
        CHECK(n1 == n2);
        CHECK(n1 >= 5);
    }

    /* record after reset re-arms the same slot (no duplicate registration) --- */
    cplus_safe_stat_record("h2_alpha", 1);
    CHECK(cplus_safe_stats_get("h2_alpha", &st) == 1);
    CHECK(st.calls == 1 && st.ok == 1 && st.fail == 0);

    if (failures == 0) {
        printf("bugclass2_telemetry: ok\n");
        return 0;
    }
    printf("bugclass2_telemetry: FAILURES=%d\n", failures);
    return 1;
}
