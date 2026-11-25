/* time_checked.c - evidence for the time/date family of the C+ hardened libc.
 *
 * Scope (public cplus_* wrappers, see libc/install-include/cplus_libc.h):
 *   time, difftime, clock, gmtime, localtime, mktime, strftime,
 *   gmtime_r, localtime_r, asctime_r, ctime_r, clock_gettime, nanosleep,
 *   timespec_get, setlocale, localeconv (locale interaction).
 *   timespec_getres is "MISSING" (no wrapper) and is reported as a SKIP.
 *
 * Design rules:
 *  - Every check runs TWICE: once with the opt-in C+ libc logger OFF and once
 *    ON.  Both outcomes must be a pass; logging must never change a result.
 *  - The host C runtime is the source of truth for wall-clock/timezone values,
 *    so wrapped calls are compared against the matching host call on identical
 *    inputs rather than against hard-coded wall-clock expectations.
 *  - Boundaries covered: known tm values, month 13 / mday 32 normalization,
 *    tm_isdst = -1/0/1, strftime buffer sizes 0/1/exact/truncated, NULL guards
 *    only where the wrapper explicitly defines them, and TZ variation in a
 *    forked child (so the parent's TZ is never mutated).
 *  - No test-side data races: the harness is single-threaded; the only shared
 *    flag (the logger enable bit) is atomic inside the library; TZ is changed
 *    only in a child's private address space.
 *  - Each check prints "<api>/<scenario> ok" or a "FINDING" line; the final
 *    line is "time_checked: <N>/<N> checks ok".  This program always exits 0
 *    so a FINDING is visible in stdout rather than hidden in a build failure.
 *
 * Standalone C11; links libcplus_hardened.a (and optionally the safecrt layer)
 * plus the host libc.
 */
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <locale.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#ifndef TIME_UTC
#define TIME_UTC 1  /* glibc hides C11 TIME_UTC under strict C99 */
#endif
#include <unistd.h>

#include "install-include/cplus_libc.h"

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

/* ---------------------------------------------------------------- helpers */

static int same_tm(const struct tm *a, const struct tm *b) {
    return a->tm_sec == b->tm_sec && a->tm_min == b->tm_min &&
           a->tm_hour == b->tm_hour && a->tm_mday == b->tm_mday &&
           a->tm_mon == b->tm_mon && a->tm_year == b->tm_year &&
           a->tm_wday == b->tm_wday && a->tm_yday == b->tm_yday &&
           a->tm_isdst == b->tm_isdst;
}

static int nsec_ok(long n) { return n >= 0 && n < 1000000000L; }

/* 2000-01-01 00:00:00, Saturday (UTC fields), isdst = -1. */
static struct tm tm_2000(void) {
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_mday = 1;
    t.tm_mon = 0;
    t.tm_year = 100;
    t.tm_wday = 6;
    t.tm_isdst = -1;
    return t;
}

/* Local noon on the given (year, mon, mday) with isdst = -1; noon avoids
 * midnight DST-transition ambiguity for the normalization checks. */
static struct tm tm_local_noon(int year, int mon, int mday) {
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_hour = 12;
    t.tm_mday = mday;
    t.tm_mon = mon;
    t.tm_year = year;
    t.tm_isdst = -1;
    return t;
}

/* ------------------------------------------------------------------- time */

static int sc_time_null(void) {
    CHECK(cplus_time(NULL) != (time_t)-1, "time(NULL) returned (time_t)-1");
    return 0;
}

static int sc_time_out(void) {
    time_t out = (time_t)0;
    time_t r = cplus_time(&out);
    CHECKF(r == out, "time(&out)=%lld out=%lld", (long long)r, (long long)out);
    CHECK(r > (time_t)1000000000L, "time() returned an implausibly old value");
    return 0;
}

/* --------------------------------------------------------------- difftime */

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

/* ------------------------------------------------------------------ clock */

static int sc_clock_nonneg(void) {
    CHECK(cplus_clock() != -1L, "clock() returned -1");
    return 0;
}

static int sc_clock_monotonic(void) {
    volatile unsigned long sink = 0;
    clock_t a = cplus_clock();
    unsigned long i;
    for (i = 0; i < 200000UL; ++i) sink += i;
    clock_t b = cplus_clock();
    CHECK(sink != 0, "work loop was optimized away");
    CHECK(b >= a, "clock() went backwards across CPU work");
    return 0;
}

/* ----------------------------------------------------------------- gmtime */

static int sc_gmtime_epoch(void) {
    time_t e = (time_t)0;
    struct tm *t = cplus_gmtime(&e);
    CHECK(t != NULL, "gmtime(0) returned NULL");
    CHECK(t->tm_year == 70 && t->tm_mon == 0 && t->tm_mday == 1 &&
              t->tm_hour == 0,
          "epoch 0 is not 1970-01-01 00:00:00 UTC");
    return 0;
}

static int sc_gmtime_2000(void) {
    time_t e = (time_t)946684800L;
    struct tm *t = cplus_gmtime(&e);
    CHECK(t != NULL, "gmtime(946684800) returned NULL");
    CHECK(t->tm_year == 100 && t->tm_mon == 0 && t->tm_mday == 1,
          "946684800 is not 2000-01-01 UTC");
    CHECK(t->tm_wday == 6, "2000-01-01 is not Saturday");
    return 0;
}

static int sc_gmtime_matches_host(void) {
    time_t e = (time_t)1000000000L;
    struct tm *a = cplus_gmtime(&e);
    struct tm *b = gmtime(&e);
    CHECK(a != NULL && b != NULL, "gmtime returned NULL");
    CHECK(same_tm(a, b), "cplus_gmtime differs from host gmtime");
    return 0;
}

/* --------------------------------------------------------------- localtime */

static int sc_localtime_matches_host(void) {
    time_t e = (time_t)1000000000L;
    struct tm *a = cplus_localtime(&e);
    struct tm *b = localtime(&e);
    CHECK(a != NULL && b != NULL, "localtime returned NULL");
    CHECK(a->tm_hour >= 0 && a->tm_hour <= 23 && a->tm_mday >= 1 &&
              a->tm_mday <= 31 && a->tm_mon >= 0 && a->tm_mon <= 11,
          "localtime fields out of range");
    CHECK(same_tm(a, b), "cplus_localtime differs from host localtime");
    return 0;
}

/* ----------------------------------------------------------------- mktime */

static int sc_mktime_normal(void) {
    struct tm a = tm_local_noon(100, 5, 15);
    struct tm b = a;
    time_t ra, rb;
    ra = cplus_mktime(&a);
    rb = mktime(&b);
    CHECK(ra != (time_t)-1, "mktime(valid tm) returned -1");
    CHECKF(ra == rb, "cplus_mktime=%lld host=%lld", (long long)ra,
           (long long)rb);
    CHECK(same_tm(&a, &b), "normalized tm differs from host mktime");
    return 0;
}

static int sc_mktime_month13(void) {
    struct tm a = tm_local_noon(100, 12, 1); /* mon == 12 -> 13th month */
    struct tm b = a;
    time_t ra, rb;
    ra = cplus_mktime(&a);
    rb = mktime(&b);
    CHECK(ra != (time_t)-1, "mktime(month 13) returned -1");
    CHECKF(ra == rb, "month 13: cplus_mktime=%lld host=%lld", (long long)ra,
           (long long)rb);
    CHECK(a.tm_mon == 0 && a.tm_year == 101,
          "month 13 did not normalize to January of the next year");
    CHECK(same_tm(&a, &b), "month 13 normalization differs from host");
    return 0;
}

static int sc_mktime_mday32(void) {
    struct tm a = tm_local_noon(100, 0, 32); /* 32nd of January */
    struct tm b = a;
    time_t ra, rb;
    ra = cplus_mktime(&a);
    rb = mktime(&b);
    CHECK(ra != (time_t)-1, "mktime(mday 32) returned -1");
    CHECKF(ra == rb, "mday 32: cplus_mktime=%lld host=%lld", (long long)ra,
           (long long)rb);
    CHECK(a.tm_mon == 1 && a.tm_mday == 1,
          "mday 32 did not normalize to February 1");
    CHECK(same_tm(&a, &b), "mday 32 normalization differs from host");
    return 0;
}

static int mktime_isdst_value(int isdst) {
    struct tm a = tm_local_noon(100, 6, 1); /* 2000-07-01 noon */
    struct tm b = a;
    time_t ra, rb;
    a.tm_isdst = isdst;
    b.tm_isdst = isdst;
    ra = cplus_mktime(&a);
    rb = mktime(&b);
    CHECK(ra != (time_t)-1, "mktime(isdst) returned -1");
    CHECKF(ra == rb, "isdst=%d: cplus_mktime=%lld host=%lld", isdst,
           (long long)ra, (long long)rb);
    CHECK(same_tm(&a, &b), "isdst normalization differs from host");
    return 0;
}

static int sc_mktime_isdst_m1(void) { return mktime_isdst_value(-1); }
static int sc_mktime_isdst_0(void) { return mktime_isdst_value(0); }
static int sc_mktime_isdst_1(void) { return mktime_isdst_value(1); }

/* --------------------------------------------------------------- strftime */

/* Compare cplus_strftime against host strftime for one (size, buffer, fmt)
 * combination, including the ISO C "size 0 / NULL destination" case. */
static int strftime_case(size_t max, int null_buf, const char *fmt) {
    struct tm t = tm_2000();
    char cbuf[64], hbuf[64];
    char *cs = null_buf ? NULL : cbuf;
    char *hs = null_buf ? NULL : hbuf;
    size_t rc, rh;
    if (!null_buf) {
        memset(cbuf, 0x7f, sizeof cbuf);
        memset(hbuf, 0x7f, sizeof hbuf);
    }
    rc = cplus_strftime(cs, max, fmt, &t);
    rh = strftime(hs, max, fmt, &t);
    CHECKF(rc == rh, "strftime(max=%zu,null=%d) return %zu != host %zu", max,
           null_buf, rc, rh);
    if (!null_buf && rc > 0 && max > 0) {
        CHECK(strcmp(cbuf, hbuf) == 0, "strftime buffer differs from host");
    }
    return 0;
}

static int sc_strftime_known(void) {
    struct tm t = tm_2000();
    char buf[64];
    size_t n = cplus_strftime(buf, sizeof buf, "%Y-%m-%d", &t);
    CHECKF(n == 10, "strftime(\"%%Y-%%m-%%d\") returned %zu, expected 10", n);
    CHECK(strcmp(buf, "2000-01-01") == 0, "strftime produced wrong date");
    return 0;
}

static int sc_strftime_size0_null(void) {
    return strftime_case(0, 1, "%Y-%m-%d");
}

static int sc_strftime_size1(void) { return strftime_case(1, 0, "%Y-%m-%d"); }

static int sc_strftime_exact(void) { return strftime_case(11, 0, "%Y-%m-%d"); }

static int sc_strftime_truncated(void) {
    return strftime_case(4, 0, "%Y-%m-%d");
}

static int sc_strftime_empty_fmt(void) { return strftime_case(32, 0, ""); }

/* ------------------------------------------------------------- gmtime_r */

static int sc_gmtime_r_valid(void) {
    time_t e = (time_t)946684800L;
    struct tm a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    CHECK(cplus_gmtime_r(&e, &a) == &a, "gmtime_r did not return result");
    CHECK(gmtime_r(&e, &b) == &b, "host gmtime_r failed");
    CHECK(same_tm(&a, &b), "cplus_gmtime_r differs from host");
    return 0;
}

static int sc_gmtime_r_null_timep(void) {
    struct tm r;
    errno = 0;
    CHECK(cplus_gmtime_r(NULL, &r) == NULL,
          "gmtime_r(NULL,result) must return NULL");
    CHECK(errno == EINVAL, "gmtime_r(NULL,result) must set EINVAL");
    return 0;
}

static int sc_gmtime_r_null_result(void) {
    time_t e = (time_t)0;
    errno = 0;
    CHECK(cplus_gmtime_r(&e, NULL) == NULL,
          "gmtime_r(time,NULL) must return NULL");
    CHECK(errno == EINVAL, "gmtime_r(time,NULL) must set EINVAL");
    return 0;
}

/* ----------------------------------------------------------- localtime_r */

static int sc_localtime_r_valid(void) {
    time_t e = (time_t)1000000000L;
    struct tm a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    CHECK(cplus_localtime_r(&e, &a) == &a, "localtime_r did not return result");
    CHECK(localtime_r(&e, &b) == &b, "host localtime_r failed");
    CHECK(same_tm(&a, &b), "cplus_localtime_r differs from host");
    return 0;
}

static int sc_localtime_r_null_timep(void) {
    struct tm r;
    errno = 0;
    CHECK(cplus_localtime_r(NULL, &r) == NULL,
          "localtime_r(NULL,result) must return NULL");
    CHECK(errno == EINVAL, "localtime_r(NULL,result) must set EINVAL");
    return 0;
}

static int sc_localtime_r_null_result(void) {
    time_t e = (time_t)0;
    errno = 0;
    CHECK(cplus_localtime_r(&e, NULL) == NULL,
          "localtime_r(time,NULL) must return NULL");
    CHECK(errno == EINVAL, "localtime_r(time,NULL) must set EINVAL");
    return 0;
}

/* -------------------------------------------------------------- asctime_r */

static int sc_asctime_r_valid(void) {
    struct tm t = tm_2000();
    char buf[64], hbuf[64];
    char *r = cplus_asctime_r(&t, buf);
    CHECK(r == buf, "asctime_r did not return the supplied buffer");
    CHECK(strchr(buf, '\n') != NULL, "asctime_r result has no newline");
    CHECK(asctime_r(&t, hbuf) == hbuf, "host asctime_r failed");
    CHECK(strcmp(buf, hbuf) == 0, "cplus_asctime_r differs from host");
    return 0;
}

static int sc_asctime_r_null_tm(void) {
    char buf[64];
    errno = 0;
    CHECK(cplus_asctime_r(NULL, buf) == NULL,
          "asctime_r(NULL,buf) must return NULL");
    CHECK(errno == EINVAL, "asctime_r(NULL,buf) must set EINVAL");
    return 0;
}

static int sc_asctime_r_null_buf(void) {
    struct tm t = tm_2000();
    errno = 0;
    CHECK(cplus_asctime_r(&t, NULL) == NULL,
          "asctime_r(tm,NULL) must return NULL");
    CHECK(errno == EINVAL, "asctime_r(tm,NULL) must set EINVAL");
    return 0;
}

/* --------------------------------------------------------------- ctime_r */

static int sc_ctime_r_valid(void) {
    time_t e = (time_t)946684800L;
    char buf[64], hbuf[64];
    char *r = cplus_ctime_r(&e, buf);
    CHECK(r == buf, "ctime_r did not return the supplied buffer");
    CHECK(strchr(buf, '\n') != NULL, "ctime_r result has no newline");
    CHECK(ctime_r(&e, hbuf) == hbuf, "host ctime_r failed");
    CHECK(strcmp(buf, hbuf) == 0, "cplus_ctime_r differs from host");
    return 0;
}

static int sc_ctime_r_null_timep(void) {
    char buf[64];
    errno = 0;
    CHECK(cplus_ctime_r(NULL, buf) == NULL,
          "ctime_r(NULL,buf) must return NULL");
    CHECK(errno == EINVAL, "ctime_r(NULL,buf) must set EINVAL");
    return 0;
}

static int sc_ctime_r_null_buf(void) {
    time_t e = (time_t)0;
    errno = 0;
    CHECK(cplus_ctime_r(&e, NULL) == NULL,
          "ctime_r(time,NULL) must return NULL");
    CHECK(errno == EINVAL, "ctime_r(time,NULL) must set EINVAL");
    return 0;
}

/* --------------------------------------------------------- clock_gettime */

static int sc_clock_gettime_realtime(void) {
    struct timespec ts;
    CHECK(cplus_clock_gettime(CLOCK_REALTIME, &ts) == 0,
          "clock_gettime(CLOCK_REALTIME) failed");
    CHECK(nsec_ok(ts.tv_nsec), "CLOCK_REALTIME tv_nsec out of range");
    CHECK(ts.tv_sec > 0, "CLOCK_REALTIME tv_sec not positive");
    return 0;
}

static int sc_clock_gettime_monotonic(void) {
    struct timespec a, b;
    CHECK(cplus_clock_gettime(CLOCK_MONOTONIC, &a) == 0,
          "clock_gettime(CLOCK_MONOTONIC) failed");
    CHECK(nsec_ok(a.tv_nsec), "CLOCK_MONOTONIC tv_nsec out of range");
    CHECK(cplus_clock_gettime(CLOCK_MONOTONIC, &b) == 0,
          "second clock_gettime(CLOCK_MONOTONIC) failed");
    CHECK(nsec_ok(b.tv_nsec), "second CLOCK_MONOTONIC tv_nsec out of range");
    CHECK(b.tv_sec > a.tv_sec ||
              (b.tv_sec == a.tv_sec && b.tv_nsec >= a.tv_nsec),
          "CLOCK_MONOTONIC went backwards");
    return 0;
}

static int sc_clock_gettime_bad_id(void) {
    struct timespec ts;
    CHECK(cplus_clock_gettime((clockid_t)-1, &ts) == -1,
          "clock_gettime(invalid id) must return -1");
    return 0;
}

/* -------------------------------------------------------------- nanosleep */

static int sc_nanosleep_zero(void) {
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = 0;
    CHECK(cplus_nanosleep(&req, NULL) == 0, "zero nanosleep(req,NULL) failed");
    return 0;
}

static int sc_nanosleep_1ms(void) {
    struct timespec req, rem;
    req.tv_sec = 0;
    req.tv_nsec = 1000000L; /* 1 ms */
    memset(&rem, 0, sizeof rem);
    CHECK(cplus_nanosleep(&req, &rem) == 0, "1ms nanosleep failed");
    return 0;
}

/* ----------------------------------------------------------- timespec_get */

static int sc_timespec_get_utc(void) {
    struct timespec ts;
    CHECK(cplus_timespec_get(&ts, TIME_UTC) == TIME_UTC,
          "timespec_get(TIME_UTC) failed");
    CHECK(ts.tv_sec > 0, "timespec_get gave non-positive seconds");
    CHECK(nsec_ok(ts.tv_nsec), "timespec_get tv_nsec out of range");
    return 0;
}

static int sc_timespec_get_bad_base(void) {
    struct timespec ts;
    CHECK(cplus_timespec_get(&ts, 0) == 0, "invalid base must return 0");
    return 0;
}

/* ------------------------------------------------- setlocale / localeconv */

static int sc_setlocale_query(void) {
    char *p = cplus_setlocale(LC_ALL, NULL);
    CHECK(p != NULL && p[0] != '\0', "setlocale(LC_ALL,NULL) gave no name");
    return 0;
}

static int sc_setlocale_c(void) {
    char *p = cplus_setlocale(LC_ALL, "C");
    CHECK(p != NULL && strcmp(p, "C") == 0, "setlocale(LC_ALL,\"C\") failed");
    return 0;
}

static int sc_setlocale_numeric(void) {
    char *p = cplus_setlocale(LC_NUMERIC, "C");
    CHECK(p != NULL && strcmp(p, "C") == 0,
          "setlocale(LC_NUMERIC,\"C\") failed");
    (void)cplus_setlocale(LC_NUMERIC, "C"); /* leave LC_NUMERIC as "C" */
    return 0;
}

static int sc_setlocale_invalid(void) {
    char before[64], after[64];
    char *p;
    char *cur = cplus_setlocale(LC_ALL, NULL);
    (void)snprintf(before, sizeof before, "%s", cur ? cur : "");
    p = cplus_setlocale(LC_ALL, "__definitely_no_such_locale__");
    cur = cplus_setlocale(LC_ALL, NULL);
    (void)snprintf(after, sizeof after, "%s", cur ? cur : "");
    CHECK(p == NULL, "invalid locale name must return NULL");
    CHECK(strcmp(before, after) == 0,
          "invalid locale name must leave the locale unchanged");
    (void)cplus_setlocale(LC_ALL, "C");
    return 0;
}

static int sc_localeconv_decimal(void) {
    struct lconv *lc;
    (void)cplus_setlocale(LC_ALL, "C");
    lc = cplus_localeconv();
    CHECK(lc != NULL, "localeconv returned NULL");
    CHECK(lc->decimal_point != NULL && strcmp(lc->decimal_point, ".") == 0,
          "C locale decimal_point is not \".\"");
    return 0;
}

static int sc_localeconv_matches_host(void) {
    struct lconv *a, *b;
    (void)cplus_setlocale(LC_ALL, "C");
    a = cplus_localeconv();
    b = localeconv();
    CHECK(a != NULL && b != NULL, "localeconv returned NULL");
    CHECK(a->decimal_point != NULL && b->decimal_point != NULL &&
              strcmp(a->decimal_point, b->decimal_point) == 0,
          "cplus_localeconv decimal_point differs from host");
    CHECK((a->grouping != NULL) == (b->grouping != NULL),
          "cplus_localeconv grouping presence differs from host");
    return 0;
}

/* ------------------------------------------------------------ TZ variation */

/* Fork a child, set TZ only there, and return the local hour for a fixed
 * instant through a pipe.  The parent's TZ/environment is never modified. */
static int local_hour_under_tz(const char *tz, time_t t, int *out) {
    int fd[2];
    pid_t pid;
    int hour = -1;
    int status = 0;

    if (pipe(fd) != 0) return -1;
    pid = fork();
    if (pid < 0) {
        (void)close(fd[0]);
        (void)close(fd[1]);
        return -1;
    }
    if (pid == 0) { /* child */
        struct tm *p;
        (void)close(fd[0]);
        (void)setenv("TZ", tz, 1);
        tzset();
        p = cplus_localtime(&t);
        hour = p ? p->tm_hour : -1;
        if (write(fd[1], &hour, sizeof hour) != (ssize_t)sizeof hour) {
            /* parent will observe EOF/short read and fail the check */
        }
        (void)close(fd[1]);
        _exit(0);
    }
    /* parent */
    (void)close(fd[1]);
    if (read(fd[0], &hour, sizeof hour) != (ssize_t)sizeof hour) hour = -1;
    (void)close(fd[0]);
    (void)waitpid(pid, &status, 0);
    *out = hour;
    return 0;
}

static int sc_tz_variation(void) {
    const time_t t = (time_t)1000000000L; /* 2001-09-09 01:46:40 UTC */
    struct tm *g = cplus_gmtime(&t);
    int utc_hour = -1, est_hour = -1;
    int expected_est;
    CHECK(g != NULL, "gmtime failed in TZ variation check");
    CHECK(local_hour_under_tz("UTC0", t, &utc_hour) == 0,
          "could not fork TZ=UTC0 child");
    CHECK(local_hour_under_tz("EST5", t, &est_hour) == 0,
          "could not fork TZ=EST5 child");
    CHECKF(utc_hour == g->tm_hour,
           "TZ=UTC0 hour %d != gmtime hour %d", utc_hour, g->tm_hour);
    expected_est = (g->tm_hour - 5 + 24) % 24; /* EST5 == UTC-5, no DST */
    CHECKF(est_hour == expected_est,
           "TZ=EST5 hour %d != expected %d", est_hour, expected_est);
    return 0;
}

/* ------------------------------------------------------------- registry */

typedef int (*check_fn)(void);
typedef struct {
    const char *api;
    const char *scenario;
    check_fn fn;
} check_t;

static const check_t g_checks[] = {
    {"time", "NULL_out", sc_time_null},
    {"time", "out_param", sc_time_out},
    {"difftime", "positive", sc_difftime_pos},
    {"difftime", "negative", sc_difftime_neg},
    {"difftime", "zero", sc_difftime_zero},
    {"difftime", "large", sc_difftime_large},
    {"clock", "not_minus_one", sc_clock_nonneg},
    {"clock", "monotonic", sc_clock_monotonic},
    {"gmtime", "epoch_1970", sc_gmtime_epoch},
    {"gmtime", "known_2000", sc_gmtime_2000},
    {"gmtime", "matches_host", sc_gmtime_matches_host},
    {"localtime", "matches_host", sc_localtime_matches_host},
    {"mktime", "normal", sc_mktime_normal},
    {"mktime", "month_13", sc_mktime_month13},
    {"mktime", "mday_32", sc_mktime_mday32},
    {"mktime", "isdst_minus1", sc_mktime_isdst_m1},
    {"mktime", "isdst_0", sc_mktime_isdst_0},
    {"mktime", "isdst_1", sc_mktime_isdst_1},
    {"strftime", "known_date", sc_strftime_known},
    {"strftime", "size0_null_buf", sc_strftime_size0_null},
    {"strftime", "size1", sc_strftime_size1},
    {"strftime", "exact_fit", sc_strftime_exact},
    {"strftime", "truncated", sc_strftime_truncated},
    {"strftime", "empty_format", sc_strftime_empty_fmt},
    {"gmtime_r", "valid", sc_gmtime_r_valid},
    {"gmtime_r", "null_timep", sc_gmtime_r_null_timep},
    {"gmtime_r", "null_result", sc_gmtime_r_null_result},
    {"localtime_r", "valid", sc_localtime_r_valid},
    {"localtime_r", "null_timep", sc_localtime_r_null_timep},
    {"localtime_r", "null_result", sc_localtime_r_null_result},
    {"asctime_r", "valid", sc_asctime_r_valid},
    {"asctime_r", "null_tm", sc_asctime_r_null_tm},
    {"asctime_r", "null_buf", sc_asctime_r_null_buf},
    {"ctime_r", "valid", sc_ctime_r_valid},
    {"ctime_r", "null_timep", sc_ctime_r_null_timep},
    {"ctime_r", "null_buf", sc_ctime_r_null_buf},
    {"clock_gettime", "realtime", sc_clock_gettime_realtime},
    {"clock_gettime", "monotonic", sc_clock_gettime_monotonic},
    {"clock_gettime", "invalid_id", sc_clock_gettime_bad_id},
    {"nanosleep", "zero_rem_null", sc_nanosleep_zero},
    {"nanosleep", "1ms_rem_out", sc_nanosleep_1ms},
    {"timespec_get", "TIME_UTC", sc_timespec_get_utc},
    {"timespec_get", "invalid_base", sc_timespec_get_bad_base},
    {"setlocale", "query", sc_setlocale_query},
    {"setlocale", "C", sc_setlocale_c},
    {"setlocale", "LC_NUMERIC_C", sc_setlocale_numeric},
    {"setlocale", "invalid_name", sc_setlocale_invalid},
    {"localeconv", "C_decimal_point", sc_localeconv_decimal},
    {"localeconv", "matches_host", sc_localeconv_matches_host},
    {"localtime", "TZ_variation_child", sc_tz_variation},
};

#define CHECK_COUNT (sizeof g_checks / sizeof g_checks[0])

/* Run one check with logging OFF and ON; both must pass (parity). */
static int run_check(check_fn fn) {
    int off_rc, on_rc;
    char off_detail[sizeof g_detail];

    cplus_libc_log_set_enabled(0);
    g_detail[0] = '\0';
    off_rc = fn();
    (void)snprintf(off_detail, sizeof off_detail, "%s", g_detail);

    cplus_libc_log_set_enabled(1);
    g_detail[0] = '\0';
    on_rc = fn();

    cplus_libc_log_set_enabled(0);
    if (off_rc == 0 && on_rc == 0) return 0;
    (void)snprintf(g_detail, sizeof g_detail,
                   "logging parity: off=%d[%s] on=%d[%s]", off_rc, off_detail,
                   on_rc, g_detail);
    return 1;
}

int main(void) {
    size_t i;
    unsigned ok_count = 0;

    (void)cplus_setlocale(LC_ALL, "C");
    cplus_libc_log_set_enabled(0);

    for (i = 0; i < CHECK_COUNT; ++i) {
        if (run_check(g_checks[i].fn) == 0) {
            ++ok_count;
            printf("time_checked: %s/%s ok\n", g_checks[i].api,
                   g_checks[i].scenario);
        } else {
            printf("time_checked: FINDING %s/%s: %s\n", g_checks[i].api,
                   g_checks[i].scenario, g_detail);
        }
    }

    /* Documented gap: no cplus_timespec_getres wrapper is shipped (C23). */
    printf("time_checked: SKIP timespec_getres: "
           "no wrapper (C23 addition, MISSING upstream)\n");

    printf("time_checked: %u/%u checks ok\n", ok_count, (unsigned)CHECK_COUNT);
    return 0;
}
