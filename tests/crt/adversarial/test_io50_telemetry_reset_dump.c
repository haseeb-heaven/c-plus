/*
 * Adversarial io50 #7: cplus_safe_stat_record / stats_reset / stats_dump.
 *
 * Raw-C trap: a telemetry registry that double-registers, counts NULL/empty
 * names, or returns stale data after a reset. This test pins the registry
 * semantics: exact counting, NULL/empty names ignored, dump clamped to `max`,
 * reset zeroing counters while preserving names, re-arming the same slot after
 * a reset (no duplicate), and truncation of over-long API names.
 */
#include <stdio.h>
#include <string.h>
#include "safe_io.h"        /* only to register an io API name below */
#include "safe_error.h"
#include "safe_log.h"
#include "safe_stats.h"

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
    size_t n0, n1, n2, n3;

    /* baseline: only the setup API ("set_constraint_mode") is guaranteed here */
    n0 = cplus_safe_stats_dump(snap, 64);
    CHECK(n0 >= 1);

    /* --- an unknown name is not found --- */
    CHECK(cplus_safe_stats_get("io50_unknown", &st) == 0);

    /* --- exact counting of a known ok/fail mix --- */
    cplus_safe_stat_record("io50_a", 1);
    cplus_safe_stat_record("io50_a", 1);
    cplus_safe_stat_record("io50_a", 1);
    cplus_safe_stat_record("io50_a", 0);
    cplus_safe_stat_record("io50_a", 0);
    CHECK(cplus_safe_stats_get("io50_a", &st) == 1);
    CHECK(st.calls == 5);
    CHECK(st.ok == 3);
    CHECK(st.fail == 2);
    CHECK(strcmp(st.api, "io50_a") == 0);

    cplus_safe_stat_record("io50_b", 0);
    CHECK(cplus_safe_stats_get("io50_b", &st) == 1);
    CHECK(st.calls == 1 && st.ok == 0 && st.fail == 1);

    /* exactly two new registrations appeared */
    n1 = cplus_safe_stats_dump(snap, 64);
    CHECK(n1 == n0 + 2);

    /* --- NULL / empty names are ignored: no new slot, no crash --- */
    cplus_safe_stat_record(NULL, 1);
    cplus_safe_stat_record("", 1);
    CHECK(cplus_safe_stats_dump(snap, 64) == n1);
    CHECK(cplus_safe_stats_get("io50_unknown", &st) == 0);

    /* --- dump clamps its output to `max` and counts what it wrote --- */
    CHECK(cplus_safe_stats_dump(snap, 1) == 1);
    CHECK(cplus_safe_stats_dump(snap, 2) == 2);
    CHECK(cplus_safe_stats_dump(snap, 0) == 0);
    CHECK(cplus_safe_stats_dump(snap, 64) == n1);

    /* every dumped entry carries a non-empty API name */
    {
        size_t k, empty = 0;
        for (k = 0; k < n1; ++k)
            if (snap[k].api[0] == '\0') empty++;
        CHECK(empty == 0);
    }

    /* --- reset zeroes the counters but preserves the registrations --- */
    cplus_safe_stats_reset();
    CHECK(cplus_safe_stats_get("io50_a", &st) == 1 && st.calls == 0 &&
          st.ok == 0 && st.fail == 0);
    CHECK(cplus_safe_stats_get("io50_b", &st) == 1 && st.calls == 0 &&
          st.ok == 0 && st.fail == 0);
    CHECK(cplus_safe_stats_dump(snap, 64) == n1);

    /* --- recording after a reset re-arms the SAME slot (no duplicate) --- */
    cplus_safe_stat_record("io50_a", 1);
    CHECK(cplus_safe_stats_get("io50_a", &st) == 1);
    CHECK(st.calls == 1 && st.ok == 1 && st.fail == 0);
    n2 = cplus_safe_stats_dump(snap, 64);
    CHECK(n2 == n1);

    /* --- a name exactly at the 23-char limit round-trips intact --- */
    CHECK(CPLUS_SAFE_STATS_API_MAX == 24);
    cplus_safe_stat_record("io50_name_exactly_23chr", 1);
    CHECK(cplus_safe_stats_get("io50_name_exactly_23chr", &st) == 1);
    CHECK(strlen(st.api) == CPLUS_SAFE_STATS_API_MAX - 1);
    n3 = cplus_safe_stats_dump(snap, 64);
    CHECK(n3 == n2 + 1);

    /* --- an over-long name is stored truncated to API_MAX-1 (23) chars --- */
    cplus_safe_stat_record("io50_this_name_is_much_longer_than_limit", 1);
    CHECK(cplus_safe_stats_dump(snap, 64) == n3 + 1);
    {
        size_t k, trunc = 0;
        for (k = 0; k < n3 + 1; ++k)
            if (strlen(snap[k].api) == CPLUS_SAFE_STATS_API_MAX - 1 &&
                strncmp(snap[k].api, "io50_this_name_is_much_", 23) == 0)
                trunc++;
        CHECK(trunc == 1);
    }

    /* --- reset with counters already zero is a safe no-op --- */
    cplus_safe_stats_reset();
    CHECK(cplus_safe_stats_get("io50_a", &st) == 1 && st.calls == 0);
    CHECK(cplus_safe_stats_dump(snap, 64) == n3 + 1);

    if (failures == 0) {
        printf("test_io50_telemetry_reset_dump: ok\n");
        return 0;
    }
    printf("test_io50_telemetry_reset_dump: FAILURES=%d\n", failures);
    return 1;
}
