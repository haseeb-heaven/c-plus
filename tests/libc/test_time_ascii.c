/* Standalone tests for the platform-backed cplus_asctime / cplus_ctime.
 *
 * NOTE: concurrent use is unsafe by design. asctime()/ctime() share one static
 * buffer and are non-reentrant per the C standard; callers needing reentrancy
 * should use cplus_asctime_r() / cplus_ctime_r() instead.
 */
#include <assert.h>
#include <time.h>
#include <string.h>

char *cplus_asctime(const struct tm *);
char *cplus_ctime(const time_t *);

int main(void) {
    /* Known date: 2000-01-01 00:00:00 (a Saturday). */
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_sec = 0;
    t.tm_min = 0;
    t.tm_hour = 0;
    t.tm_mday = 1;
    t.tm_mon = 0;    /* January */
    t.tm_year = 100; /* 2000 */
    t.tm_wday = 6;   /* Saturday (Jan 1 2000) */
    t.tm_yday = 0;
    t.tm_isdst = -1;

    char *s = cplus_asctime(&t);
    assert(s != NULL);
    assert(strcmp(s, "Sat Jan  1 00:00:00 2000\n") == 0);

    /* ctime() on a host mktime()-derived time_t avoids timezone-sensitive
     * hard-coded epoch values while keeping the expected wall-clock year. */
    struct tm local;
    memset(&local, 0, sizeof(local));
    local.tm_sec = 0;
    local.tm_min = 0;
    local.tm_hour = 12;
    local.tm_mday = 1;
    local.tm_mon = 0;
    local.tm_year = 123; /* 2023 */
    local.tm_isdst = -1;
    time_t epoch = mktime(&local);
    assert(epoch != (time_t)-1);

    char *c = cplus_ctime(&epoch);
    assert(c != NULL);
    /* The classic "26-character" form counts the terminating NUL; the visible
     * string is 25 chars on this host (some libcs emit 26). Verify structure. */
    size_t n = strlen(c);
    assert(n == 25 || n == 26);
    assert(c[n - 1] == '\n');
    assert(strstr(c, "2023") != NULL);

    /* NULL argument error paths. */
    assert(cplus_asctime(NULL) == NULL);
    assert(cplus_ctime(NULL) == NULL);

    return 0;
}
