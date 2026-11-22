/*
 * Adversarial io50 #6: telemetry exactness for a known io sequence.
 *
 * Drives a fixed, counted sequence through cplus_fopen_s / cplus_fwrite_s /
 * cplus_fread_s / cplus_fclose_s and asserts the per-API calls/ok/fail triple
 * recorded by cplus_safe_stats_get matches exactly. Also checks that reading a
 * counter is non-destructive, that an unknown name is not fabricated, and that
 * cplus_safe_stats_reset zeroes the counters while keeping the names visible.
 */
#include <stdio.h>
#include <string.h>
#include "safe_io.h"
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
    cplus_safe_stats_reset();   /* zero counters after the setup calls above */

    const char *path = "/tmp/cplus_io50_telemetry.tmp";
    const char *payload = "ABCD";
    FILE *f = NULL;
    char r[16];
    cplus_safe_stat st;

    /* --- fopen_s: 2 rejects + 1 success -> calls 3 / ok 1 / fail 2 --- */
    CHECK(cplus_fopen_s(NULL, "wb") == NULL);
    CHECK(cplus_fopen_s(path, NULL) == NULL);
    f = cplus_fopen_s(path, "wb+");
    CHECK(f != NULL);
    CHECK(cplus_safe_stats_get("fopen_s", &st) == 1);
    CHECK(st.calls == 3);
    CHECK(st.ok == 1);
    CHECK(st.fail == 2);
    CHECK(strcmp(st.api, "fopen_s") == 0);

    /* --- fwrite_s: 1 ok + 4 rejects -> calls 5 / ok 1 / fail 4 --- */
    CHECK(cplus_fwrite_s(payload, 4, 1, 4, f) == 4);       /* ok   */
    CHECK(cplus_fwrite_s(NULL, 4, 1, 1, f) == 0);          /* fail */
    CHECK(cplus_fwrite_s(payload, 4, 0, 1, f) == 0);       /* fail */
    CHECK(cplus_fwrite_s(payload, 4, 1, 0, f) == 0);       /* fail */
    CHECK(cplus_fwrite_s(payload, 4, 1, 1, NULL) == 0);    /* fail */
    CHECK(cplus_safe_stats_get("fwrite_s", &st) == 1);
    CHECK(st.calls == 5);
    CHECK(st.ok == 1);
    CHECK(st.fail == 4);
    CHECK(strcmp(st.api, "fwrite_s") == 0);

    /* --- fread_s: 1 ok + 3 rejects -> calls 4 / ok 1 / fail 3 --- */
    rewind(f);
    CHECK(cplus_fread_s(r, sizeof r, 1, 4, f) == 4);       /* ok   */
    CHECK(cplus_fread_s(NULL, sizeof r, 1, 4, f) == 0);    /* fail */
    CHECK(cplus_fread_s(r, sizeof r, 0, 4, f) == 0);       /* fail */
    CHECK(cplus_fread_s(r, sizeof r, 1, 0, f) == 0);       /* fail */
    CHECK(cplus_safe_stats_get("fread_s", &st) == 1);
    CHECK(st.calls == 4);
    CHECK(st.ok == 1);
    CHECK(st.fail == 3);
    CHECK(strcmp(st.api, "fread_s") == 0);

    /* --- fclose_s: 1 ok + 1 double-close reject -> calls 2 / ok 1 / fail 1 --- */
    CHECK(cplus_fclose_s(f) == 0);                         /* ok   */
    CHECK(cplus_fclose_s(f) == EOF);                       /* fail */
    CHECK(cplus_safe_stats_get("fclose_s", &st) == 1);
    CHECK(st.calls == 2);
    CHECK(st.ok == 1);
    CHECK(st.fail == 1);
    CHECK(strcmp(st.api, "fclose_s") == 0);

    /* --- reading a counter twice is non-destructive (identical snapshots) --- */
    {
        cplus_safe_stat a, b;
        CHECK(cplus_safe_stats_get("fopen_s", &a) == 1);
        CHECK(cplus_safe_stats_get("fopen_s", &b) == 1);
        CHECK(a.calls == b.calls && a.ok == b.ok && a.fail == b.fail);
    }

    /* --- an unknown API name is reported as not found (no fabricated entry) --- */
    CHECK(cplus_safe_stats_get("io50_nope", &st) == 0);

    /* --- reset zeroes every counter but keeps the names queryable --- */
    cplus_safe_stats_reset();
    CHECK(cplus_safe_stats_get("fopen_s", &st) == 1 && st.calls == 0 &&
          st.ok == 0 && st.fail == 0);
    CHECK(cplus_safe_stats_get("fwrite_s", &st) == 1 && st.calls == 0 &&
          st.ok == 0 && st.fail == 0);
    CHECK(cplus_safe_stats_get("fread_s", &st) == 1 && st.calls == 0 &&
          st.ok == 0 && st.fail == 0);
    CHECK(cplus_safe_stats_get("fclose_s", &st) == 1 && st.calls == 0 &&
          st.ok == 0 && st.fail == 0);

    remove(path);

    if (failures == 0) {
        printf("test_io50_telemetry_calls: ok\n");
        return 0;
    }
    printf("test_io50_telemetry_calls: FAILURES=%d\n", failures);
    return 1;
}
