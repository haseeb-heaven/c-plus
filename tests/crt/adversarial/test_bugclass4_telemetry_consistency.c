/*
 * Bug CLASS 4e: telemetry that disagrees with reality.
 *
 * The checked C+ safe CRT exposes per-API counters (calls / ok / fail). This
 * test drives a KNOWN sequence across three different APIs and proves
 * record/get/dump all agree exactly, that reset zeroes the counters while
 * keeping the registrations, and that unknown/NULL names are ignored.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_stats.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h" /* ESNOSPC */

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

/* Locate an entry by name within a dump snapshot. Returns 1 and fills *out. */
static int find_in_dump(cplus_safe_stat *snap, size_t n, const char *api,
                        cplus_safe_stat *out) {
    size_t i;
    for (i = 0; i < n; ++i) {
        if (strcmp(snap[i].api, api) == 0) {
            *out = snap[i];
            return 1;
        }
    }
    return 0;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_stats_reset();   /* zero out anything recorded during setup */

    cplus_safe_stat st;
    cplus_safe_stat snap[64];

    /* --- drive a KNOWN call/ok/fail sequence across three APIs --- */
    {
        char d[8];
        void *p = NULL;

        /* memcpy_s: 3 ok + 1 fail => calls 4, ok 3, fail 1 */
        CHECK(cplus_memcpy_s(d, sizeof d, "abc", 3) == 0);
        CHECK(cplus_memcpy_s(d, sizeof d, "defg", 4) == 0);
        CHECK(cplus_memcpy_s(d, sizeof d, "hi", 2) == 0);
        CHECK(cplus_memcpy_s(d, 2, "abcd", 4) != 0);

        /* strcpy_s: 2 ok + 1 fail => calls 3, ok 2, fail 1 */
        CHECK(cplus_strcpy_s(d, sizeof d, "hello") == 0);
        CHECK(cplus_strcpy_s(d, sizeof d, "world") == 0);
        CHECK(cplus_strcpy_s(d, 4, "toolong") != 0);

        /* malloc_s: 1 ok + 1 fail => calls 2, ok 1, fail 1 */
        p = malloc_s(16);
        CHECK(p != NULL);
        CHECK(malloc_s((size_t)-1) == NULL);
        free_s(p);
    }

    /* --- get() must report the EXACT counts for each API --- */
    CHECK(cplus_safe_stats_get("memcpy_s", &st) == 1);
    CHECK(strcmp(st.api, "memcpy_s") == 0);
    CHECK(st.calls == 4);
    CHECK(st.ok == 3);
    CHECK(st.fail == 1);
    CHECK(st.calls == st.ok + st.fail);

    CHECK(cplus_safe_stats_get("strcpy_s", &st) == 1);
    CHECK(st.calls == 3);
    CHECK(st.ok == 2);
    CHECK(st.fail == 1);
    CHECK(st.calls == st.ok + st.fail);

    CHECK(cplus_safe_stats_get("malloc_s", &st) == 1);
    CHECK(st.calls == 2);
    CHECK(st.ok == 1);
    CHECK(st.fail == 1);
    CHECK(st.calls == st.ok + st.fail);

    /* --- dump() must agree with get() for all three --- */
    {
        size_t n = cplus_safe_stats_dump(snap, 64);
        cplus_safe_stat e;
        CHECK(n >= 3);
        CHECK(find_in_dump(snap, n, "memcpy_s", &e) == 1);
        CHECK(e.calls == 4 && e.ok == 3 && e.fail == 1);
        CHECK(find_in_dump(snap, n, "strcpy_s", &e) == 1);
        CHECK(e.calls == 3 && e.ok == 2 && e.fail == 1);
        CHECK(find_in_dump(snap, n, "malloc_s", &e) == 1);
        CHECK(e.calls == 2 && e.ok == 1 && e.fail == 1);
    }

    /* --- unknown names are ignored, never fabricated --- */
    CHECK(cplus_safe_stats_get("h3_does_not_exist", &st) == 0);

    /* --- NULL / empty API names are ignored: no new slot, no crash --- */
    {
        size_t before = cplus_safe_stats_dump(snap, 64);
        cplus_safe_stat_record(NULL, 1);
        cplus_safe_stat_record("", 1);
        CHECK(cplus_safe_stats_dump(snap, 64) == before);
        CHECK(cplus_safe_stats_get("memcpy_s", &st) == 1);
        CHECK(st.calls == 4 && st.ok == 3 && st.fail == 1);
    }

    /* --- reset zeroes counters but keeps registrations --- */
    {
        size_t n_before = cplus_safe_stats_dump(snap, 64);
        cplus_safe_stats_reset();

        CHECK(cplus_safe_stats_get("memcpy_s", &st) == 1);   /* still there */
        CHECK(st.calls == 0 && st.ok == 0 && st.fail == 0);
        CHECK(cplus_safe_stats_get("strcpy_s", &st) == 1);
        CHECK(st.calls == 0 && st.ok == 0 && st.fail == 0);
        CHECK(cplus_safe_stats_get("malloc_s", &st) == 1);
        CHECK(st.calls == 0 && st.ok == 0 && st.fail == 0);

        /* registration count is unchanged by reset */
        CHECK(cplus_safe_stats_dump(snap, 64) == n_before);
    }

    /* --- recording after reset re-arms the existing slot (no duplicate) --- */
    {
        size_t n_before = cplus_safe_stats_dump(snap, 64);
        cplus_safe_stat_record("memcpy_s", 1);
        CHECK(cplus_safe_stats_get("memcpy_s", &st) == 1);
        CHECK(st.calls == 1 && st.ok == 1 && st.fail == 0);
        CHECK(cplus_safe_stats_dump(snap, 64) == n_before);
    }

    if (failures == 0) {
        printf("bugclass4_telemetry_consistency: ok\n");
        return 0;
    }
    printf("bugclass4_telemetry_consistency: FAILURES=%d\n", failures);
    return 1;
}
