/* 100 DISTINCT runtime scenarios for the C+ libc <time.h>, <locale.h> and
 * <fenv.h> wrappers (time, difftime, clock, gmtime, localtime, mktime,
 * strftime, gmtime_r, localtime_r, asctime_r, ctime_r, cplus_timespec_get,
 * clock_gettime, nanosleep, setlocale, localeconv, feclearexcept,
 * fetestexcept, fegetround, fesetround, fegetenv, fesetenv,
 * fegetexceptflag, fesetexceptflag, feholdexcept, feupdateenv,
 * feraiseexcept).
 *
 * Design rules:
 *  - Every case is executed TWICE: once with the opt-in C+ libc logger OFF
 *    and once with it ON. Both outcomes must agree (logging OFF/ON parity).
 *    The ambient FP environment is captured/restored around each run so the
 *    two runs are independent and no FP state leaks between cases.
 *  - No genuine UB: NULL is only passed where the wrapper has an explicit
 *    defined guard, or where ISO C defines the argument (e.g. time(NULL),
 *    setlocale(cat, NULL), strftime(NULL, 0, ...)). Hostile 0xFF fexcept_t
 *    objects are valid unsigned-short objects, not trap representations.
 *  - Host libc is the source of truth for wall-clock/timezone values;
 *    comparisons against host functions are used where formats/range vary.
 *  - Each case prints:
 *      hundred_time_fenv: case <n> <method> <scenario> <OUTCOME>
 *    A mismatch additionally emits a "hundred_time_fenv: FINDING ..." line.
 *
 * Standalone C11; links libcplus_hardened.a and host -lm.
 */
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <fenv.h>
#include <locale.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "install-include/cplus_libc.h"

#define SCEN_COUNT 100

static char g_detail[320];

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            (void)snprintf(g_detail, sizeof g_detail, "%s", (msg));           \
            return 1;                                                         \
        }                                                                     \
    } while (0)

#define CHECKF(cond, fmt, ...)                                                \
    do {                                                                      \
        if (!(cond)) {                                                        \
            (void)snprintf(g_detail, sizeof g_detail, (fmt), __VA_ARGS__);    \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int same_tm(const struct tm *a, const struct tm *b) {
    return a->tm_sec == b->tm_sec && a->tm_min == b->tm_min &&
           a->tm_hour == b->tm_hour && a->tm_mday == b->tm_mday &&
           a->tm_mon == b->tm_mon && a->tm_year == b->tm_year &&
           a->tm_wday == b->tm_wday && a->tm_yday == b->tm_yday &&
           a->tm_isdst == b->tm_isdst;
}

/* 2000-01-01 00:00:00, Saturday (UTC fields), isdst = -1. */
static struct tm tm_2000(void) {
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_sec = 0;
    t.tm_min = 0;
    t.tm_hour = 0;
    t.tm_mday = 1;
    t.tm_mon = 0;
    t.tm_year = 100;
    t.tm_wday = 6;
    t.tm_yday = 0;
    t.tm_isdst = -1;
    return t;
}

static struct tm tm_local_noon(int year, int mon, int mday) {
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_sec = 0;
    t.tm_min = 0;
    t.tm_hour = 12;
    t.tm_mday = mday;
    t.tm_mon = mon;
    t.tm_year = year;
    t.tm_isdst = -1;
    return t;
}

static int nsec_ok(long n) { return n >= 0 && n < 1000000000L; }

/* ------------------------------------------------------------------ time */

static int sc_time_null_out(void) {
    time_t r = cplus_time(NULL);
    CHECK(r != (time_t)-1, "time(NULL) returned (time_t)-1");
    return 0;
}

static int sc_time_out_param(void) {
    time_t out = (time_t)0;
    time_t r = cplus_time(&out);
    CHECKF(r == out, "time(&out)=%ld out=%ld", (long)r, (long)out);
    return 0;
}

static int sc_time_monotonic(void) {
    time_t a = cplus_time(NULL);
    time_t b = cplus_time(NULL);
    CHECK(b >= a, "second time() went backwards");
    return 0;
}

static int sc_time_plausible(void) {
    time_t now = cplus_time(NULL);
    struct tm *t = cplus_gmtime(&now);
    CHECK(t != NULL, "gmtime(current time) returned NULL");
    CHECKF(t->tm_year >= 110, "unlikely year %d", t->tm_year + 1900);
    return 0;
}

static int sc_difftime_pos(void) {
    CHECK(cplus_difftime((time_t)100, (time_t)80) == 20.0, "100-80 != 20");
    return 0;
}

static int sc_difftime_neg(void) {
    CHECK(cplus_difftime((time_t)80, (time_t)100) == -20.0, "80-100 != -20");
    return 0;
}

static int sc_difftime_zero(void) {
    CHECK(cplus_difftime((time_t)77, (time_t)77) == 0.0, "x-x != 0");
    return 0;
}

static int sc_difftime_large(void) {
    CHECK(cplus_difftime((time_t)2000000000L, (time_t)0) == 2000000000.0,
          "large span mismatch");
    return 0;
}

static int sc_clock_nonneg(void) {
    clock_t c = cplus_clock();
    CHECK(c != (clock_t)-1, "clock() returned (clock_t)-1");
    return 0;
}

static int sc_clock_work(void) {
    volatile unsigned long sink = 0;
    clock_t c0 = cplus_clock();
    unsigned long i;
    for (i = 0; i < 2000000UL; ++i) sink += i;
    clock_t c1 = cplus_clock();
    CHECK(sink != 0, "work loop optimized away");
    CHECK(c1 >= c0, "clock() went backwards over CPU work");
    return 0;
}

static int sc_gmtime_epoch(void) {
    time_t e = (time_t)0;
    struct tm *t = cplus_gmtime(&e);
    CHECK(t != NULL, "gmtime(0) returned NULL");
    CHECK(t->tm_year == 70 && t->tm_mon == 0 && t->tm_mday == 1,
          "epoch 0 is not 1970-01-01 UTC");
    return 0;
}

static int sc_gmtime_2000(void) {
    time_t e = (time_t)946684800L;
    struct tm *t = cplus_gmtime(&e);
    CHECK(t != NULL, "gmtime(946684800) returned NULL");
    CHECK(t->tm_year == 100 && t->tm_mon == 0 && t->tm_mday == 1,
          "epoch is not 2000-01-01 UTC");
    CHECK(t->tm_wday == 6, "2000-01-01 is not Saturday");
    return 0;
}

static int sc_gmtime_leap(void) {
    time_t e = (time_t)951782400L; /* 2000-02-29 00:00:00 UTC */
    struct tm *t = cplus_gmtime(&e);
    CHECK(t != NULL, "gmtime(leap day) returned NULL");
    CHECK(t->tm_year == 100 && t->tm_mon == 1 && t->tm_mday == 29,
          "leap day fields wrong");
    return 0;
}

static int sc_gmtime_static_reuse(void) {
    time_t e1 = (time_t)0, e2 = (time_t)946684800L;
    struct tm *p1 = cplus_gmtime(&e1);
    struct tm a;
    struct tm *p2;
    struct tm b;
    CHECK(p1 != NULL, "first gmtime returned NULL");
    a = *p1;
    p2 = cplus_gmtime(&e2);
    CHECK(p2 != NULL, "second gmtime returned NULL");
    b = *p2;
    CHECK(a.tm_year != b.tm_year, "static buffer did not change on reuse");
    return 0;
}

static int sc_gmtime_negative(void) {
    time_t e = (time_t)-1;
    struct tm *t = cplus_gmtime(&e);
    CHECK(t != NULL, "gmtime(-1) returned NULL");
    CHECK(t->tm_year == 69 && t->tm_mon == 11 && t->tm_mday == 31,
          "epoch -1 is not 1969-12-31 UTC");
    return 0;
}

static int sc_localtime_midday(void) {
    struct tm src = tm_local_noon(123, 5, 15);
    time_t e = mktime(&src);
    struct tm *t;
    CHECK(e != (time_t)-1, "mktime setup failed");
    t = cplus_localtime(&e);
    CHECK(t != NULL, "localtime returned NULL");
    CHECK(t->tm_year == 123 && t->tm_mon == 5 && t->tm_mday == 15 &&
              t->tm_hour == 12,
          "localtime fields differ from mktime input");
    return 0;
}

static int sc_localtime_wday(void) {
    struct tm src = tm_local_noon(121, 6, 15);
    time_t e = mktime(&src);
    struct tm *t;
    CHECK(e != (time_t)-1, "mktime setup failed");
    CHECK(src.tm_wday >= 0 && src.tm_wday <= 6, "mktime gave bad wday");
    t = cplus_localtime(&e);
    CHECK(t != NULL, "localtime returned NULL");
    CHECKF(t->tm_wday == src.tm_wday, "wday %d != mktime wday %d",
           t->tm_wday, src.tm_wday);
    return 0;
}

static int sc_localtime_static_reuse(void) {
    struct tm a, b;
    struct tm *p;
    struct tm s1 = tm_local_noon(100, 0, 1);
    struct tm s2 = tm_local_noon(123, 0, 1);
    time_t e1 = mktime(&s1), e2 = mktime(&s2);
    CHECK(e1 != (time_t)-1 && e2 != (time_t)-1, "mktime setup failed");
    p = cplus_localtime(&e1);
    CHECK(p != NULL, "first localtime returned NULL");
    a = *p;
    p = cplus_localtime(&e2);
    CHECK(p != NULL, "second localtime returned NULL");
    b = *p;
    CHECK(a.tm_year != b.tm_year, "localtime static buffer did not change");
    return 0;
}

static int sc_localtime_isdst(void) {
    struct tm s1 = tm_local_noon(120, 0, 15);
    struct tm s2 = tm_local_noon(120, 6, 15);
    time_t e1 = mktime(&s1), e2 = mktime(&s2);
    struct tm *t1, *t2;
    CHECK(e1 != (time_t)-1 && e2 != (time_t)-1, "mktime setup failed");
    t1 = cplus_localtime(&e1);
    t2 = cplus_localtime(&e2);
    CHECK(t1 != NULL && t2 != NULL, "localtime returned NULL");
    CHECK(t1->tm_isdst >= -1 && t1->tm_isdst <= 1, "Jan isdst out of range");
    CHECK(t2->tm_isdst >= -1 && t2->tm_isdst <= 1, "Jul isdst out of range");
    return 0;
}

static int sc_mktime_roundtrip(void) {
    struct tm src = tm_local_noon(120, 2, 10);
    time_t e = cplus_mktime(&src);
    struct tm *back;
    CHECK(e != (time_t)-1, "mktime failed");
    back = cplus_localtime(&e);
    CHECK(back != NULL, "localtime returned NULL");
    CHECK(back->tm_year == 120 && back->tm_mon == 2 && back->tm_mday == 10 &&
              back->tm_hour == 12,
          "mktime/localtime round trip mismatch");
    return 0;
}

static int sc_mktime_year_zero(void) {
    struct tm t = tm_local_noon(0, 0, 1);
    time_t e = cplus_mktime(&t);
    struct tm *back;
    CHECK(e != (time_t)-1, "mktime(tm_year=0) failed");
    back = cplus_localtime(&e);
    CHECK(back != NULL, "localtime returned NULL");
    CHECKF(back->tm_year == 0, "tm_year=0 became %d", back->tm_year);
    return 0;
}

static int sc_mktime_month13(void) {
    struct tm t = tm_local_noon(100, 12, 1);
    time_t e = cplus_mktime(&t);
    struct tm *back;
    CHECK(e != (time_t)-1, "mktime(month=13) failed");
    back = cplus_localtime(&e);
    CHECK(back != NULL, "localtime returned NULL");
    CHECK(back->tm_year == 101 && back->tm_mon == 0,
          "month 13 did not normalize to next January");
    return 0;
}

static int sc_mktime_mday32(void) {
    struct tm t = tm_local_noon(100, 0, 32);
    time_t e = cplus_mktime(&t);
    struct tm *back;
    CHECK(e != (time_t)-1, "mktime(mday=32) failed");
    back = cplus_localtime(&e);
    CHECK(back != NULL, "localtime returned NULL");
    CHECK(back->tm_mon == 1 && back->tm_mday == 1,
          "mday 32 did not normalize to Feb 1");
    return 0;
}

static int sc_mktime_isdst_m1(void) {
    struct tm t = tm_local_noon(120, 6, 15);
    t.tm_isdst = -1;
    CHECK(cplus_mktime(&t) != (time_t)-1, "mktime(isdst=-1) failed");
    CHECK(t.tm_isdst >= -1 && t.tm_isdst <= 1, "isdst=-1 result out of range");
    return 0;
}

static int sc_mktime_isdst_0(void) {
    struct tm t = tm_local_noon(120, 0, 15);
    t.tm_isdst = 0;
    CHECK(cplus_mktime(&t) != (time_t)-1, "mktime(isdst=0) failed");
    CHECK(t.tm_isdst >= 0 && t.tm_isdst <= 1, "isdst=0 result out of range");
    return 0;
}

static int sc_mktime_isdst_1(void) {
    struct tm t = tm_local_noon(120, 6, 15);
    t.tm_isdst = 1;
    CHECK(cplus_mktime(&t) != (time_t)-1, "mktime(isdst=1) failed");
    CHECK(t.tm_isdst >= 0 && t.tm_isdst <= 1, "isdst=1 result out of range");
    return 0;
}

static int sc_mktime_hour25(void) {
    struct tm t = tm_local_noon(100, 0, 1);
    t.tm_hour = 25;
    time_t e = cplus_mktime(&t);
    struct tm *back;
    CHECK(e != (time_t)-1, "mktime(hour=25) failed");
    back = cplus_localtime(&e);
    CHECK(back != NULL, "localtime returned NULL");
    CHECK(back->tm_mday == 2 && back->tm_hour == 1,
          "hour 25 did not normalize to next day 01:00");
    return 0;
}

static int sc_strftime_known(void) {
    struct tm t = tm_2000();
    char got[64], want[64];
    size_t rg = cplus_strftime(got, sizeof got, "%Y-%m-%d", &t);
    size_t rw = strftime(want, sizeof want, "%Y-%m-%d", &t);
    CHECKF(rg == rw && rg == 10 && strcmp(got, want) == 0,
           "cplus=%zu host=%zu got='%s'", rg, rw, got);
    CHECK(strcmp(got, "2000-01-01") == 0, "unexpected formatted date");
    return 0;
}

static int sc_strftime_zero_null(void) {
    struct tm t = tm_2000();
    CHECK(cplus_strftime(NULL, 0, "%Y", &t) == 0,
          "strftime(NULL,0,...) must return 0");
    return 0;
}

static int sc_strftime_one_trunc(void) {
    struct tm t = tm_2000();
    char buf[1] = {'X'};
    CHECK(cplus_strftime(buf, 1, "%Y-%m-%d", &t) == 0,
          "strftime with size 1 must report truncation");
    return 0;
}

static int sc_strftime_exact_fit(void) {
    struct tm t = tm_2000();
    char buf[11];
    memset(buf, 0, sizeof buf);
    CHECK(cplus_strftime(buf, 11, "%Y-%m-%d", &t) == 10,
          "exact-fit size must return strlen");
    CHECK(strcmp(buf, "2000-01-01") == 0, "exact-fit content wrong");
    return 0;
}

static int sc_strftime_trunc_zero(void) {
    struct tm t = tm_2000();
    char buf[4] = {'X', 'X', 'X', 'X'};
    CHECK(cplus_strftime(buf, 4, "%Y-%m-%d", &t) == 0,
          "truncated strftime must return 0");
    return 0;
}

static int sc_strftime_empty_fmt(void) {
    struct tm t = tm_2000();
    char buf[8] = {'x', 0, 0, 0, 0, 0, 0, 0};
    CHECK(cplus_strftime(buf, sizeof buf, "", &t) == 0,
          "empty format must return 0");
    CHECK(buf[0] == '\0', "empty format must write a NUL when size > 0");
    return 0;
}

static int sc_strftime_weekday(void) {
    struct tm t = tm_2000();
    char got[32], want[32];
    size_t rg = cplus_strftime(got, sizeof got, "%A", &t);
    size_t rw = strftime(want, sizeof want, "%A", &t);
    CHECKF(rg == rw && strcmp(got, want) == 0,
           "weekday cplus='%s' host='%s'", got, want);
    return 0;
}

static int sc_gmtime_r_valid(void) {
    time_t e = (time_t)0;
    struct tm a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    CHECK(cplus_gmtime_r(&e, &a) == &a, "gmtime_r did not return result");
    CHECK(gmtime_r(&e, &b) == &b, "host gmtime_r failed");
    CHECK(same_tm(&a, &b), "gmtime_r differs from host");
    CHECK(a.tm_year == 70, "epoch 0 year wrong");
    return 0;
}

static int sc_gmtime_r_null_timep(void) {
    struct tm out;
    errno = 0;
    CHECK(cplus_gmtime_r(NULL, &out) == NULL, "NULL timep not rejected");
    CHECK(errno == EINVAL, "NULL timep errno != EINVAL");
    return 0;
}

static int sc_gmtime_r_null_result(void) {
    time_t e = (time_t)0;
    errno = 0;
    CHECK(cplus_gmtime_r(&e, NULL) == NULL, "NULL result not rejected");
    CHECK(errno == EINVAL, "NULL result errno != EINVAL");
    return 0;
}

static int sc_gmtime_r_both_null(void) {
    errno = 0;
    CHECK(cplus_gmtime_r(NULL, NULL) == NULL, "both NULL not rejected");
    CHECK(errno == EINVAL, "both NULL errno != EINVAL");
    return 0;
}

static int sc_gmtime_r_future(void) {
    time_t e = (time_t)4102444800L; /* 2100-01-01 UTC */
    struct tm a;
    memset(&a, 0, sizeof a);
    CHECK(cplus_gmtime_r(&e, &a) == &a, "gmtime_r(2100) failed");
    CHECKF(a.tm_year == 200, "2100 year field %d", a.tm_year);
    return 0;
}

static int sc_localtime_r_valid(void) {
    struct tm s = tm_local_noon(123, 2, 14);
    time_t e = mktime(&s);
    struct tm a, b;
    CHECK(e != (time_t)-1, "mktime setup failed");
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    CHECK(cplus_localtime_r(&e, &a) == &a, "localtime_r did not return result");
    CHECK(localtime_r(&e, &b) == &b, "host localtime_r failed");
    CHECK(same_tm(&a, &b), "localtime_r differs from host");
    return 0;
}

static int sc_localtime_r_null_timep(void) {
    struct tm out;
    errno = 0;
    CHECK(cplus_localtime_r(NULL, &out) == NULL, "NULL timep not rejected");
    CHECK(errno == EINVAL, "NULL timep errno != EINVAL");
    return 0;
}

static int sc_localtime_r_null_result(void) {
    time_t e = (time_t)0;
    errno = 0;
    CHECK(cplus_localtime_r(&e, NULL) == NULL, "NULL result not rejected");
    CHECK(errno == EINVAL, "NULL result errno != EINVAL");
    return 0;
}

static int sc_localtime_r_two_buffers(void) {
    struct tm s1 = tm_local_noon(100, 0, 1), s2 = tm_local_noon(123, 0, 1);
    time_t e1 = mktime(&s1), e2 = mktime(&s2);
    struct tm a, b;
    CHECK(e1 != (time_t)-1 && e2 != (time_t)-1, "mktime setup failed");
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    CHECK(cplus_localtime_r(&e1, &a) == &a, "first localtime_r failed");
    CHECK(cplus_localtime_r(&e2, &b) == &b, "second localtime_r failed");
    CHECK(&a != &b, "distinct caller buffers must stay distinct");
    CHECK(a.tm_year != b.tm_year, "independent results have same year");
    return 0;
}

static int sc_asctime_r_known(void) {
    struct tm t = tm_2000();
    char buf[32], want[32];
    char *r = cplus_asctime_r(&t, buf);
    CHECK(r == buf, "asctime_r did not return buffer");
    CHECK(strcmp(buf, "Sat Jan  1 00:00:00 2000\n") == 0,
          "asctime_r format mismatch");
    CHECK(asctime_r(&t, want) == want && strcmp(buf, want) == 0,
          "asctime_r differs from host");
    return 0;
}

static int sc_asctime_r_null_tm(void) {
    char buf[32];
    errno = 0;
    CHECK(cplus_asctime_r(NULL, buf) == NULL, "NULL tm not rejected");
    CHECK(errno == EINVAL, "NULL tm errno != EINVAL");
    return 0;
}

static int sc_asctime_r_null_buf(void) {
    struct tm t = tm_2000();
    errno = 0;
    CHECK(cplus_asctime_r(&t, NULL) == NULL, "NULL buf not rejected");
    CHECK(errno == EINVAL, "NULL buf errno != EINVAL");
    return 0;
}

static int sc_asctime_r_both_null(void) {
    errno = 0;
    CHECK(cplus_asctime_r(NULL, NULL) == NULL, "both NULL not rejected");
    CHECK(errno == EINVAL, "both NULL errno != EINVAL");
    return 0;
}

static int sc_ctime_r_known(void) {
    time_t e = (time_t)946684800L;
    char buf[32], want[32];
    size_t n;
    CHECK(cplus_ctime_r(&e, buf) == buf, "ctime_r did not return buffer");
    CHECK(ctime_r(&e, want) == want && strcmp(buf, want) == 0,
          "ctime_r differs from host");
    n = strlen(buf);
    CHECK(n >= 24 && n <= 26, "ctime_r length out of range");
    CHECK(buf[n - 1] == '\n', "ctime_r result has no trailing newline");
    return 0;
}

static int sc_ctime_r_null_timep(void) {
    char buf[32];
    errno = 0;
    CHECK(cplus_ctime_r(NULL, buf) == NULL, "NULL timep not rejected");
    CHECK(errno == EINVAL, "NULL timep errno != EINVAL");
    return 0;
}

static int sc_ctime_r_null_buf(void) {
    time_t e = (time_t)0;
    errno = 0;
    CHECK(cplus_ctime_r(&e, NULL) == NULL, "NULL buf not rejected");
    CHECK(errno == EINVAL, "NULL buf errno != EINVAL");
    return 0;
}

static int sc_ctime_r_both_null(void) {
    errno = 0;
    CHECK(cplus_ctime_r(NULL, NULL) == NULL, "both NULL not rejected");
    CHECK(errno == EINVAL, "both NULL errno != EINVAL");
    return 0;
}

static int sc_timespec_get_utc(void) {
    struct timespec ts;
    memset(&ts, 0, sizeof ts);
    CHECK(cplus_timespec_get(&ts, TIME_UTC) == TIME_UTC,
          "timespec_get(TIME_UTC) failed");
    CHECK(ts.tv_sec > 0, "timespec_get gave non-positive seconds");
    CHECK(nsec_ok(ts.tv_nsec), "timespec_get nanoseconds out of range");
    return 0;
}

static int sc_timespec_get_bad_base(void) {
    struct timespec ts;
    memset(&ts, 0, sizeof ts);
    CHECK(cplus_timespec_get(&ts, 0) == 0, "invalid base must return 0");
    return 0;
}

static int sc_clock_gettime_realtime(void) {
    struct timespec ts;
    memset(&ts, 0, sizeof ts);
    CHECK(cplus_clock_gettime(CLOCK_REALTIME, &ts) == 0,
          "clock_gettime(CLOCK_REALTIME) failed");
    CHECK(ts.tv_sec > 0, "CLOCK_REALTIME seconds non-positive");
    CHECK(nsec_ok(ts.tv_nsec), "CLOCK_REALTIME nanoseconds out of range");
    return 0;
}

static int sc_clock_gettime_monotonic(void) {
    struct timespec ts;
    memset(&ts, 0, sizeof ts);
    CHECK(cplus_clock_gettime(CLOCK_MONOTONIC, &ts) == 0,
          "clock_gettime(CLOCK_MONOTONIC) failed");
    CHECK(ts.tv_sec >= 0, "CLOCK_MONOTONIC seconds negative");
    CHECK(nsec_ok(ts.tv_nsec), "CLOCK_MONOTONIC nanoseconds out of range");
    return 0;
}

static int sc_clock_gettime_bad_id(void) {
    struct timespec ts;
    memset(&ts, 0, sizeof ts);
    CHECK(cplus_clock_gettime((clockid_t)-1, &ts) == -1,
          "invalid clock id must return -1");
    return 0;
}

static int sc_nanosleep_zero(void) {
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = 0;
    CHECK(cplus_nanosleep(&req, NULL) == 0, "zero nanosleep failed");
    return 0;
}

static int sc_nanosleep_1ms(void) {
    struct timespec req, rem;
    req.tv_sec = 0;
    req.tv_nsec = 1000000L;
    rem.tv_sec = 0;
    rem.tv_nsec = 0;
    CHECK(cplus_nanosleep(&req, &rem) == 0, "1ms nanosleep failed");
    return 0;
}

static int sc_nanosleep_zero_rem(void) {
    struct timespec req, rem;
    req.tv_sec = 0;
    req.tv_nsec = 0;
    rem.tv_sec = (time_t)123;
    rem.tv_nsec = 456L;
    CHECK(cplus_nanosleep(&req, &rem) == 0, "zero nanosleep with rem failed");
    return 0;
}

/* ---------------------------------------------------------------- locale */

static int sc_locale_query(void) {
    char *p = cplus_setlocale(LC_ALL, NULL);
    CHECK(p != NULL && p[0] != '\0', "setlocale(LC_ALL,NULL) gave no name");
    return 0;
}

static int sc_locale_set_c(void) {
    char *p = cplus_setlocale(LC_ALL, "C");
    CHECK(p != NULL && strcmp(p, "C") == 0, "setlocale(LC_ALL,\"C\") failed");
    return 0;
}

static int sc_locale_invalid(void) {
    char before[64] = {0};
    char *q = cplus_setlocale(LC_ALL, NULL);
    char *p;
    CHECK(q != NULL, "locale query failed");
    (void)snprintf(before, sizeof before, "%s", q);
    p = cplus_setlocale(LC_ALL, "__definitely_no_such_locale__");
    CHECK(p == NULL, "invalid locale unexpectedly accepted");
    q = cplus_setlocale(LC_ALL, NULL);
    CHECK(q != NULL && strcmp(before, q) == 0, "invalid locale changed state");
    return 0;
}

static int sc_locale_numeric(void) {
    char *p = cplus_setlocale(LC_NUMERIC, "C");
    CHECK(p != NULL && strcmp(p, "C") == 0, "setlocale(LC_NUMERIC) failed");
    return 0;
}

static int sc_localeconv_decimal(void) {
    struct lconv *lc;
    (void)cplus_setlocale(LC_ALL, "C");
    lc = cplus_localeconv();
    CHECK(lc != NULL, "localeconv returned NULL");
    CHECK(lc->decimal_point != NULL && strcmp(lc->decimal_point, ".") == 0,
          "C locale decimal_point is not '.'");
    return 0;
}

static int sc_localeconv_fields(void) {
    struct lconv *lc;
    (void)cplus_setlocale(LC_ALL, "C");
    lc = cplus_localeconv();
    CHECK(lc != NULL, "localeconv returned NULL");
    CHECK(lc->thousands_sep != NULL, "thousands_sep is NULL");
    CHECK(lc->grouping != NULL, "grouping is NULL");
    CHECK(lc->int_curr_symbol != NULL, "int_curr_symbol is NULL");
    CHECK(lc->currency_symbol != NULL, "currency_symbol is NULL");
    return 0;
}

/* ------------------------------------------------------------------ fenv */

static int sc_feclear_all(void) {
    CHECK(cplus_feraiseexcept(FE_ALL_EXCEPT) == 0, "raise all failed");
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "clear all failed");
    CHECK(cplus_fetestexcept(FE_ALL_EXCEPT) == 0, "flags remain after clear");
    return 0;
}

static int sc_feclear_invalid(void) {
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_INVALID) == 0, "raise INVALID failed");
    CHECK(cplus_feclearexcept(FE_INVALID) == 0, "clear INVALID failed");
    CHECK((cplus_fetestexcept(FE_INVALID) & FE_INVALID) == 0,
          "INVALID still set");
    return 0;
}

static int sc_feclear_zero_mask(void) {
    int m;
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_INVALID | FE_OVERFLOW) == 0,
          "raise subset failed");
    CHECK(cplus_feclearexcept(0) == 0, "clear with mask 0 must succeed");
    m = cplus_fetestexcept(FE_ALL_EXCEPT);
    CHECK((m & FE_INVALID) && (m & FE_OVERFLOW), "mask 0 cleared a flag");
    return 0;
}

static int sc_feclear_subset(void) {
    int m;
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_INVALID | FE_OVERFLOW) == 0,
          "raise subset failed");
    CHECK(cplus_feclearexcept(FE_INVALID) == 0, "clear INVALID failed");
    m = cplus_fetestexcept(FE_ALL_EXCEPT);
    CHECK((m & FE_INVALID) == 0, "INVALID survived clear");
    CHECK((m & FE_OVERFLOW) != 0, "OVERFLOW lost by subset clear");
    return 0;
}

static int sc_fetest_clean(void) {
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "clear all failed");
    CHECK(cplus_fetestexcept(FE_ALL_EXCEPT) == 0, "clean env has flags");
    return 0;
}

static int sc_fetest_zero_mask(void) {
    CHECK(cplus_fetestexcept(0) == 0, "test with mask 0 must return 0");
    return 0;
}

static int sc_fetest_all_four(void) {
    int m;
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_INVALID) == 0, "raise INVALID failed");
    CHECK(cplus_feraiseexcept(FE_DIVBYZERO) == 0, "raise DIVBYZERO failed");
    CHECK(cplus_feraiseexcept(FE_OVERFLOW) == 0, "raise OVERFLOW failed");
    CHECK(cplus_feraiseexcept(FE_UNDERFLOW) == 0, "raise UNDERFLOW failed");
    m = cplus_fetestexcept(FE_ALL_EXCEPT);
    CHECK((m & FE_INVALID) && (m & FE_DIVBYZERO) && (m & FE_OVERFLOW) &&
              (m & FE_UNDERFLOW),
          "not all four FE flags observed");
    return 0;
}

static int sc_fetest_masking(void) {
    int m;
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_OVERFLOW) == 0, "raise OVERFLOW failed");
    m = cplus_fetestexcept(FE_INVALID);
    CHECK((m & FE_INVALID) == 0, "unraised flag reported with narrow mask");
    m = cplus_fetestexcept(FE_OVERFLOW);
    CHECK((m & FE_OVERFLOW) != 0, "raised flag missing with narrow mask");
    return 0;
}

static int sc_round_valid(void) {
    int r = cplus_fegetround();
    CHECK(r == FE_TONEAREST || r == FE_DOWNWARD || r == FE_UPWARD ||
              r == FE_TOWARDZERO,
          "fegetround returned an unknown mode");
    return 0;
}

static int sc_round_nearest(void) {
    CHECK(cplus_fesetround(FE_TONEAREST) == 0, "set TONEAREST failed");
    CHECK(cplus_fegetround() == FE_TONEAREST, "TONEAREST round trip failed");
    return 0;
}

static int sc_round_down(void) {
    CHECK(cplus_fesetround(FE_DOWNWARD) == 0, "set DOWNWARD failed");
    CHECK(cplus_fegetround() == FE_DOWNWARD, "DOWNWARD round trip failed");
    return 0;
}

static int sc_round_up(void) {
    CHECK(cplus_fesetround(FE_UPWARD) == 0, "set UPWARD failed");
    CHECK(cplus_fegetround() == FE_UPWARD, "UPWARD round trip failed");
    return 0;
}

static int sc_round_zero(void) {
    CHECK(cplus_fesetround(FE_TOWARDZERO) == 0, "set TOWARDZERO failed");
    CHECK(cplus_fegetround() == FE_TOWARDZERO, "TOWARDZERO round trip failed");
    return 0;
}

static int sc_round_invalid(void) {
    int before = cplus_fegetround();
    int r = cplus_fesetround(0x7f);
    CHECK(r != 0, "invalid rounding mode was accepted");
    CHECK(cplus_fegetround() == before, "invalid mode changed the environment");
    return 0;
}

static int sc_env_roundtrip(void) {
    fenv_t saved;
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_fesetround(FE_UPWARD) == 0, "set UPWARD failed");
    CHECK(cplus_feraiseexcept(FE_INVALID) == 0, "raise INVALID failed");
    CHECK(cplus_fegetenv(&saved) == 0, "fegetenv failed");
    CHECK(cplus_fesetround(FE_DOWNWARD) == 0, "set DOWNWARD failed");
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "clear failed");
    CHECK(cplus_fesetenv(&saved) == 0, "fesetenv failed");
    CHECK(cplus_fegetround() == FE_UPWARD, "rounding mode not restored");
    CHECK((cplus_fetestexcept(FE_INVALID) & FE_INVALID) != 0,
          "pending flag not restored");
    return 0;
}

static int sc_env_pending(void) {
    fenv_t saved;
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_OVERFLOW) == 0, "raise OVERFLOW failed");
    CHECK(cplus_fegetenv(&saved) == 0, "fegetenv failed");
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "clear failed");
    CHECK(cplus_fetestexcept(FE_OVERFLOW) == 0, "clear did not remove flag");
    CHECK(cplus_fesetenv(&saved) == 0, "fesetenv failed");
    CHECK((cplus_fetestexcept(FE_OVERFLOW) & FE_OVERFLOW) != 0,
          "saved pending flag not restored");
    return 0;
}

static int sc_env_clean_restore(void) {
    fenv_t saved;
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_fegetenv(&saved) == 0, "fegetenv failed");
    CHECK(cplus_feraiseexcept(FE_INVALID) == 0, "raise INVALID failed");
    CHECK(cplus_fesetenv(&saved) == 0, "fesetenv failed");
    CHECK(cplus_fetestexcept(FE_ALL_EXCEPT) == 0,
          "clean environment not restored");
    return 0;
}

static int sc_raise_invalid(void) {
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_INVALID) == 0, "raise INVALID failed");
    CHECK((cplus_fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) != 0,
          "FE_INVALID not observed");
    return 0;
}

static int sc_raise_divbyzero(void) {
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_DIVBYZERO) == 0, "raise DIVBYZERO failed");
    CHECK((cplus_fetestexcept(FE_ALL_EXCEPT) & FE_DIVBYZERO) != 0,
          "FE_DIVBYZERO not observed");
    return 0;
}

static int sc_raise_overflow(void) {
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_OVERFLOW) == 0, "raise OVERFLOW failed");
    CHECK((cplus_fetestexcept(FE_ALL_EXCEPT) & FE_OVERFLOW) != 0,
          "FE_OVERFLOW not observed");
    return 0;
}

static int sc_raise_underflow(void) {
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_UNDERFLOW) == 0, "raise UNDERFLOW failed");
    CHECK((cplus_fetestexcept(FE_ALL_EXCEPT) & FE_UNDERFLOW) != 0,
          "FE_UNDERFLOW not observed");
    return 0;
}

static int sc_raise_all_four(void) {
    int mask = FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW;
    int m;
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(mask) == 0, "raise all-four mask failed");
    m = cplus_fetestexcept(FE_ALL_EXCEPT);
    CHECK((m & mask) == mask, "all-four mask not fully raised");
    return 0;
}

static int sc_raise_zero(void) {
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(0) == 0, "raise with mask 0 must succeed");
    CHECK(cplus_fetestexcept(FE_ALL_EXCEPT) == 0, "mask 0 raised a flag");
    return 0;
}

static int sc_getflag_single(void) {
    fexcept_t fl;
    memset(&fl, 0, sizeof fl);
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_INVALID) == 0, "raise INVALID failed");
    CHECK(cplus_fegetexceptflag(&fl, FE_INVALID) == 0, "get flag failed");
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "clear failed");
    CHECK(cplus_fesetexceptflag(&fl, FE_INVALID) == 0, "set flag failed");
    CHECK((cplus_fetestexcept(FE_INVALID) & FE_INVALID) != 0,
          "saved flag not restored");
    return 0;
}

static int sc_getflag_all(void) {
    fexcept_t fl;
    memset(&fl, 0, sizeof fl);
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_ALL_EXCEPT) == 0, "raise all failed");
    CHECK(cplus_fegetexceptflag(&fl, FE_ALL_EXCEPT) == 0, "get all flags failed");
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "clear failed");
    CHECK(cplus_fesetexceptflag(&fl, FE_ALL_EXCEPT) == 0, "set all flags failed");
    CHECK((cplus_fetestexcept(FE_ALL_EXCEPT) & FE_ALL_EXCEPT) == FE_ALL_EXCEPT,
          "all flags not restored");
    return 0;
}

static int sc_getflag_null(void) {
    int before;
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    before = cplus_fetestexcept(FE_ALL_EXCEPT);
    errno = 0;
    CHECK(cplus_fegetexceptflag(NULL, FE_ALL_EXCEPT) != 0,
          "NULL flagp not rejected");
    CHECK(errno == EINVAL, "NULL flagp errno != EINVAL");
    CHECK(cplus_fetestexcept(FE_ALL_EXCEPT) == before,
          "NULL get changed the flags");
    return 0;
}

static int sc_getflag_null_zero_mask(void) {
    errno = 0;
    CHECK(cplus_fegetexceptflag(NULL, 0) != 0,
          "NULL flagp with mask 0 not rejected");
    CHECK(errno == EINVAL, "NULL flagp(mask 0) errno != EINVAL");
    return 0;
}

static int sc_setflag_restore(void) {
    fexcept_t fl;
    memset(&fl, 0, sizeof fl);
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feraiseexcept(FE_OVERFLOW) == 0, "raise OVERFLOW failed");
    CHECK(cplus_fegetexceptflag(&fl, FE_OVERFLOW) == 0, "get flag failed");
    CHECK(cplus_feclearexcept(FE_OVERFLOW) == 0, "clear OVERFLOW failed");
    CHECK((cplus_fetestexcept(FE_OVERFLOW) & FE_OVERFLOW) == 0,
          "OVERFLOW still set before restore");
    CHECK(cplus_fesetexceptflag(&fl, FE_OVERFLOW) == 0, "set flag failed");
    CHECK((cplus_fetestexcept(FE_OVERFLOW) & FE_OVERFLOW) != 0,
          "OVERFLOW not restored by fesetexceptflag");
    return 0;
}

static int sc_setflag_hostile_masked(void) {
    fexcept_t hostile;
    int m;
    memset(&hostile, 0xFF, sizeof hostile); /* valid object, hostile contents */
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    {
        int r = cplus_fesetexceptflag(&hostile, FE_INVALID);
        CHECK(r == 0 || r == -1, "hostile masked flag call crashed/odd status");
    }
    m = cplus_fetestexcept(FE_ALL_EXCEPT);
    CHECK((m & ~FE_INVALID) == 0, "hostile bits leaked outside the mask");
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "cleanup clear failed");
    return 0;
}

static int sc_setflag_hostile_all(void) {
    fexcept_t hostile;
    int r;
    memset(&hostile, 0xFF, sizeof hostile);
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    r = cplus_fesetexceptflag(&hostile, FE_ALL_EXCEPT);
    CHECK(r == 0 || r == -1, "hostile all-mask flag call had odd status");
    CHECK(cplus_fetestexcept(FE_ALL_EXCEPT) >= 0, "flag test after hostile set");
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "cleanup clear failed");
    return 0;
}

static int sc_hold_clean(void) {
    fenv_t held;
    int round = cplus_fegetround();
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feholdexcept(&held) == 0, "feholdexcept failed");
    CHECK(cplus_fetestexcept(FE_ALL_EXCEPT) == 0, "hold left flags set");
    CHECK(cplus_fegetround() == round, "hold changed rounding mode");
    CHECK(cplus_feupdateenv(&held) == 0, "feupdateenv failed");
    CHECK(cplus_fetestexcept(FE_ALL_EXCEPT) == 0, "update left flags set");
    CHECK(cplus_fegetround() == round, "update changed rounding mode");
    return 0;
}

static int sc_hold_pending(void) {
    fenv_t held;
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_fesetround(FE_UPWARD) == 0, "set UPWARD failed");
    CHECK(cplus_feraiseexcept(FE_INVALID) == 0, "raise INVALID failed");
    CHECK(cplus_feholdexcept(&held) == 0, "feholdexcept failed");
    CHECK(cplus_fetestexcept(FE_ALL_EXCEPT) == 0, "hold did not clear flags");
    CHECK(cplus_feraiseexcept(FE_OVERFLOW) == 0, "raise OVERFLOW failed");
    CHECK(cplus_feupdateenv(&held) == 0, "feupdateenv failed");
    CHECK((cplus_fetestexcept(FE_OVERFLOW) & FE_OVERFLOW) != 0,
          "pending OVERFLOW not re-raised by update");
    CHECK(cplus_fegetround() == FE_UPWARD, "held rounding mode not restored");
    return 0;
}

static int sc_hold_null(void) {
    int round = cplus_fegetround();
    errno = 0;
    CHECK(cplus_feholdexcept(NULL) != 0, "NULL envp not rejected");
    CHECK(errno == EINVAL, "NULL envp errno != EINVAL");
    CHECK(cplus_fegetround() == round, "NULL hold changed rounding mode");
    return 0;
}

static int sc_update_no_pending(void) {
    fenv_t held;
    int round = cplus_fegetround();
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_feholdexcept(&held) == 0, "feholdexcept failed");
    CHECK(cplus_feupdateenv(&held) == 0, "feupdateenv failed");
    CHECK(cplus_fetestexcept(FE_ALL_EXCEPT) == 0,
          "update without pending set a flag");
    CHECK(cplus_fegetround() == round, "rounding mode changed");
    return 0;
}

static int sc_update_pending(void) {
    fenv_t held;
    CHECK(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "initial clear failed");
    CHECK(cplus_fesetround(FE_TOWARDZERO) == 0, "set TOWARDZERO failed");
    CHECK(cplus_feholdexcept(&held) == 0, "feholdexcept failed");
    CHECK(cplus_feraiseexcept(FE_UNDERFLOW) == 0, "raise UNDERFLOW failed");
    CHECK(cplus_feupdateenv(&held) == 0, "feupdateenv failed");
    CHECK((cplus_fetestexcept(FE_UNDERFLOW) & FE_UNDERFLOW) != 0,
          "pending UNDERFLOW not re-raised");
    CHECK(cplus_fegetround() == FE_TOWARDZERO, "rounding mode not restored");
    return 0;
}

/* -------------------------------------------------------------- dispatch */

typedef int (*scen_fn)(void);

typedef struct {
    const char *method;
    const char *scenario;
    scen_fn fn;
} case_t;

static const case_t g_cases[SCEN_COUNT] = {
    {"time", "null_out_returns_epoch", sc_time_null_out},
    {"time", "out_param_matches_return", sc_time_out_param},
    {"time", "two_calls_monotonic", sc_time_monotonic},
    {"time", "epoch_year_plausible", sc_time_plausible},
    {"difftime", "positive_delta", sc_difftime_pos},
    {"difftime", "negative_delta", sc_difftime_neg},
    {"difftime", "equal_zero", sc_difftime_zero},
    {"difftime", "large_span", sc_difftime_large},
    {"clock", "nonnegative", sc_clock_nonneg},
    {"clock", "advances_over_work", sc_clock_work},
    {"gmtime", "epoch_1970", sc_gmtime_epoch},
    {"gmtime", "known_2000_date", sc_gmtime_2000},
    {"gmtime", "leap_day_2000", sc_gmtime_leap},
    {"gmtime", "static_reuse_two_epochs", sc_gmtime_static_reuse},
    {"gmtime", "negative_epoch", sc_gmtime_negative},
    {"localtime", "known_midday", sc_localtime_midday},
    {"localtime", "wday_consistency", sc_localtime_wday},
    {"localtime", "static_reuse_local_epochs", sc_localtime_static_reuse},
    {"localtime", "isdst_range", sc_localtime_isdst},
    {"mktime", "known_date_roundtrip", sc_mktime_roundtrip},
    {"mktime", "year_zero_normalizes", sc_mktime_year_zero},
    {"mktime", "month_13_normalizes", sc_mktime_month13},
    {"mktime", "mday_32_normalizes", sc_mktime_mday32},
    {"mktime", "isdst_minus_one", sc_mktime_isdst_m1},
    {"mktime", "isdst_zero", sc_mktime_isdst_0},
    {"mktime", "isdst_one", sc_mktime_isdst_1},
    {"mktime", "hour_25_normalizes", sc_mktime_hour25},
    {"strftime", "known_format_exact", sc_strftime_known},
    {"strftime", "buffer_zero_null", sc_strftime_zero_null},
    {"strftime", "buffer_one_truncated", sc_strftime_one_trunc},
    {"strftime", "buffer_exact_fit", sc_strftime_exact_fit},
    {"strftime", "buffer_truncated_zero", sc_strftime_trunc_zero},
    {"strftime", "empty_format", sc_strftime_empty_fmt},
    {"strftime", "weekday_name", sc_strftime_weekday},
    {"gmtime_r", "valid_epoch", sc_gmtime_r_valid},
    {"gmtime_r", "null_timep_guard", sc_gmtime_r_null_timep},
    {"gmtime_r", "null_result_buf", sc_gmtime_r_null_result},
    {"gmtime_r", "both_null_args", sc_gmtime_r_both_null},
    {"gmtime_r", "far_future", sc_gmtime_r_future},
    {"localtime_r", "valid_mktime", sc_localtime_r_valid},
    {"localtime_r", "null_timep_rejected", sc_localtime_r_null_timep},
    {"localtime_r", "null_result_out", sc_localtime_r_null_result},
    {"localtime_r", "independent_buffers", sc_localtime_r_two_buffers},
    {"asctime_r", "known_saturday", sc_asctime_r_known},
    {"asctime_r", "null_tm", sc_asctime_r_null_tm},
    {"asctime_r", "null_buf_guard", sc_asctime_r_null_buf},
    {"asctime_r", "tm_and_buf_null", sc_asctime_r_both_null},
    {"ctime_r", "known_epoch", sc_ctime_r_known},
    {"ctime_r", "null_timep_guard_ctime", sc_ctime_r_null_timep},
    {"ctime_r", "null_buf_rejected", sc_ctime_r_null_buf},
    {"ctime_r", "timep_and_buf_null", sc_ctime_r_both_null},
    {"timespec_get", "utc_valid", sc_timespec_get_utc},
    {"timespec_get", "invalid_base_zero", sc_timespec_get_bad_base},
    {"clock_gettime", "realtime_valid", sc_clock_gettime_realtime},
    {"clock_gettime", "monotonic_valid", sc_clock_gettime_monotonic},
    {"clock_gettime", "invalid_clock_id", sc_clock_gettime_bad_id},
    {"nanosleep", "zero_no_rem", sc_nanosleep_zero},
    {"nanosleep", "one_ms_with_rem", sc_nanosleep_1ms},
    {"nanosleep", "zero_with_rem", sc_nanosleep_zero_rem},
    {"setlocale", "query_null", sc_locale_query},
    {"setlocale", "set_c_all", sc_locale_set_c},
    {"setlocale", "invalid_name", sc_locale_invalid},
    {"setlocale", "numeric_c", sc_locale_numeric},
    {"localeconv", "decimal_point_c", sc_localeconv_decimal},
    {"localeconv", "nonnull_fields", sc_localeconv_fields},
    {"feclearexcept", "clear_all_after_raise", sc_feclear_all},
    {"feclearexcept", "clear_single_invalid", sc_feclear_invalid},
    {"feclearexcept", "clear_mask_zero", sc_feclear_zero_mask},
    {"feclearexcept", "clear_subset_preserves", sc_feclear_subset},
    {"fetestexcept", "clean_zero", sc_fetest_clean},
    {"fetestexcept", "zero_mask_query", sc_fetest_zero_mask},
    {"fetestexcept", "all_four_raised", sc_fetest_all_four},
    {"fetestexcept", "narrow_mask", sc_fetest_masking},
    {"fegetround", "valid_mode", sc_round_valid},
    {"fesetround", "tonearest_roundtrip", sc_round_nearest},
    {"fesetround", "downward_roundtrip", sc_round_down},
    {"fesetround", "upward_roundtrip", sc_round_up},
    {"fesetround", "towardzero_roundtrip", sc_round_zero},
    {"fesetround", "invalid_mode", sc_round_invalid},
    {"fegetenv", "rounding_roundtrip", sc_env_roundtrip},
    {"fegetenv", "captures_pending", sc_env_pending},
    {"fesetenv", "restores_clean_state", sc_env_clean_restore},
    {"feraiseexcept", "invalid_flag", sc_raise_invalid},
    {"feraiseexcept", "divbyzero_flag", sc_raise_divbyzero},
    {"feraiseexcept", "overflow_flag", sc_raise_overflow},
    {"feraiseexcept", "underflow_flag", sc_raise_underflow},
    {"feraiseexcept", "all_four_combined", sc_raise_all_four},
    {"feraiseexcept", "zero_mask_noop", sc_raise_zero},
    {"fegetexceptflag", "single_flag", sc_getflag_single},
    {"fegetexceptflag", "all_flags", sc_getflag_all},
    {"fegetexceptflag", "null_flagp", sc_getflag_null},
    {"fegetexceptflag", "null_flagp_mask_zero", sc_getflag_null_zero_mask},
    {"fesetexceptflag", "restore_single", sc_setflag_restore},
    {"fesetexceptflag", "hostile_0ff_masked", sc_setflag_hostile_masked},
    {"fesetexceptflag", "hostile_0ff_all", sc_setflag_hostile_all},
    {"feholdexcept", "hold_clean_env", sc_hold_clean},
    {"feholdexcept", "with_pending", sc_hold_pending},
    {"feholdexcept", "null_envp", sc_hold_null},
    {"feupdateenv", "update_clean_env", sc_update_no_pending},
    {"feupdateenv", "pending_reraise", sc_update_pending},
};

/* Execute one case twice (logging OFF then ON) under the same ambient FP
 * environment, and require both runs to agree and succeed. */
static int run_case(scen_fn fn) {
    fenv_t ambient;
    char off_detail[sizeof g_detail];
    int off_rc, on_rc;

    cplus_libc_log_set_enabled(0);
    (void)fegetenv(&ambient);
    g_detail[0] = '\0';
    off_rc = fn();
    (void)snprintf(off_detail, sizeof off_detail, "%s", g_detail);
    (void)fesetenv(&ambient);

    cplus_libc_log_set_enabled(1);
    g_detail[0] = '\0';
    on_rc = fn();
    (void)fesetenv(&ambient);
    cplus_libc_log_set_enabled(0);

    if (off_rc == 0 && on_rc == 0) return 0;
    (void)snprintf(g_detail, sizeof g_detail,
                   "log-off=%d[%s] log-on=%d[%s]", off_rc, off_detail, on_rc,
                   g_detail);
    return 1;
}

int main(void) {
    int i, ok_count = 0;
    fenv_t ambient;

    (void)setlocale(LC_ALL, "C");
    (void)fegetenv(&ambient);
    cplus_libc_log_set_enabled(0);

    for (i = 0; i < SCEN_COUNT; ++i) {
        int bad = run_case(g_cases[i].fn);
        if (!bad) {
            ++ok_count;
            printf("hundred_time_fenv: case %d %s %s ok\n", i + 1,
                   g_cases[i].method, g_cases[i].scenario);
        } else {
            printf("hundred_time_fenv: case %d %s %s FINDING\n", i + 1,
                   g_cases[i].method, g_cases[i].scenario);
            printf("hundred_time_fenv: FINDING case %d %s %s: %s\n", i + 1,
                   g_cases[i].method, g_cases[i].scenario, g_detail);
        }
    }

    (void)fesetenv(&ambient);
    cplus_libc_log_set_enabled(0);
    printf("hundred_time_fenv: %d/%d scenarios ok\n", ok_count, SCEN_COUNT);
    return 0;
}
