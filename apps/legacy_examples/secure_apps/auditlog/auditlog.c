/*
 * auditlog.c -- a small, SECURE audit-log application built on the additive
 * cplus_violation_log_* API (libc/src/runtime/violation_log.c).
 *
 * This is a TEACHING example, not a safety proof.  It demonstrates:
 *   - every path comes from argv; nothing is hard-coded by the application;
 *   - a fixed, deterministic sequence of audit events (code/kind/ptr/size)
 *     with SYNTHETIC pointer values, so the transcript is reproducible;
 *   - refusal of hostile inputs, each printed as a REFUSED line with the
 *     error code actually observed from the API;
 *   - read-back of a fixed subset of lines to show append/format correctness.
 *
 * Build (from the repository root):
 *   /usr/bin/clang -std=c99 -pthread -Ilibc/install-include \
 *     examples/secure_apps/auditlog/auditlog.c \
 *     build/libc/libcplus_hardened.a -o <bin>
 * Run:
 *   timeout 30 <bin> <log-path-in-a-temp-dir>
 *
 * NOTE ON THE "OVERSIZED KIND" CASE (important, and deliberately honest):
 * The library does NOT return an error for a long `kind` -- it truncates it
 * to 96 bytes at format time (VIO_KIND_CAP).  A silent truncation would
 * corrupt an audit record, so this SECURE APP adds its own up-front policy
 * check and refuses any kind longer than the backend's faithful capacity.
 * That refusal is APP_POLICY (app-level), not an errno returned by the API.
 * The other refusals below ARE real API errno codes (EINVAL / EBADF).
 */

#include "cplus_violation_log.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Application policy: do not let the backend truncate an audit record.
 * The backend (VIO_KIND_CAP) faithfully stores at most 96 bytes of `kind`. */
#define AUDIT_KIND_CAP 96u

static int g_scenarios = 0;
static int g_passed = 0;
static int g_failed = 0;
static int g_refused = 0;

/* One expectation that must hold; does not correspond to a refusal. */
static void check(int cond, const char *what) {
    ++g_scenarios;
    if (cond) {
        ++g_passed;
    } else {
        ++g_failed;
        printf("FAIL: %s\n", what);
    }
}

static const char *errno_name(int rc) {
    switch (rc) {
    case 0:
        return "OK";
    case EINVAL:
        return "EINVAL";
    case EBADF:
        return "EBADF";
    case EFBIG:
        return "EFBIG";
    default:
        return "ERRNO";
    }
}

/* A refusal that the API reports through a returned errno code. */
static void refuse_api(const char *label, int rc, int want) {
    ++g_scenarios;
    printf("REFUSED %s code=%s api_ret=%d\n", label, errno_name(rc), rc);
    if (rc == want) {
        ++g_passed;
        ++g_refused;
    } else {
        ++g_failed;
        printf("FAIL: %s expected %s got %s\n", label, errno_name(want),
               errno_name(rc));
    }
}

/* A refusal enforced by the application itself (no API call is made). */
static void refuse_policy(const char *label, size_t got, size_t cap) {
    ++g_scenarios;
    printf("REFUSED %s code=APP_POLICY api_ret=n/a kind_len=%lu cap=%lu "
           "detail=library_truncates_long_kind\n",
           label, (unsigned long)got, (unsigned long)cap);
    if (got > cap) {
        ++g_passed;
        ++g_refused;
    } else {
        ++g_failed;
        printf("FAIL: %s kind_len=%lu is not over cap=%lu\n", label,
               (unsigned long)got, (unsigned long)cap);
    }
}

/* Fixed audit records.  Pointers are SYNTHETIC numeric values: no stack or
 * heap address ever enters the log, keeping the transcript deterministic. */
struct audit_event {
    int code;
    const char *kind;
    unsigned long long addr;
    size_t size;
};

static const struct audit_event g_events[] = {
    {100, "auth.login", 0x1000ULL, 4},
    {101, "auth.logout", 0x2000ULL, 0},
    {102, "acl.denied", 0x3000ULL, 16},
    {103, "cfg.changed", 0x4000ULL, 8},
    {104, "integrity.ok", 0x5000ULL, 32},
};
#define EVENT_COUNT (sizeof(g_events) / sizeof(g_events[0]))

static const struct audit_event g_append = {105, "session.end", 0x6000ULL, 1};

/* Expected on-disk lines, in append order. */
static const char *const g_expected[EVENT_COUNT + 1] = {
    "100 auth.login 0x1000 4",
    "101 auth.logout 0x2000 0",
    "102 acl.denied 0x3000 16",
    "103 cfg.changed 0x4000 8",
    "104 integrity.ok 0x5000 32",
    "105 session.end 0x6000 1",
};

static size_t read_all(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    size_t n;
    if (f == NULL)
        return (size_t)-1;
    n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    fclose(f);
    return n;
}

static size_t count_lines(const char *buf) {
    size_t count = 0;
    const char *p = buf;
    while (*p != '\0') {
        if (*p == '\n')
            ++count;
        ++p;
    }
    return count;
}

/* Copy newline-terminated line number `idx` (0-based) into `out`. */
static int get_line(const char *buf, size_t idx, char *out, size_t cap) {
    size_t i = 0;
    const char *p = buf;
    while (p != NULL && *p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t len = (eol != NULL) ? (size_t)(eol - p) : strlen(p);
        if (i == idx) {
            if (len >= cap)
                return -1;
            memcpy(out, p, len);
            out[len] = '\0';
            return 0;
        }
        ++i;
        if (eol == NULL)
            break;
        p = eol + 1;
    }
    return -1;
}

static int emit(const struct audit_event *ev) {
    int rc = cplus_violation_log_event(ev->code, ev->kind,
                                       (void *)(uintptr_t)ev->addr, ev->size);
    printf("EVENT code=%d kind=%s ptr=0x%llx size=%lu rc=%d\n", ev->code,
           ev->kind, ev->addr, (unsigned long)ev->size, rc);
    return rc;
}

int main(int argc, char **argv) {
    char buf[8192];
    char line[256];
    char oversized[AUDIT_KIND_CAP + 25];
    size_t n, i;
    unsigned long expected_count;

    /* No hard-coded path: the caller supplies the log location. */
    if (argc < 2 || argv[1][0] == '\0') {
        fprintf(stderr, "usage: auditlog <log-path>\n");
        return 2;
    }

    /* ---- closed state / pre-open refusals ------------------------------ */
    cplus_violation_log_close(); /* safe no-op when already closed */
    check(cplus_violation_log_count() == 0UL, "initial count is zero");

    refuse_api("event_before_open",
               cplus_violation_log_event(900, "not-open", (void *)(uintptr_t)1,
                                         1),
               EBADF);
    refuse_api("open_null_path", cplus_violation_log_open(NULL), EINVAL);
    refuse_api("open_empty_path", cplus_violation_log_open(""), EINVAL);

    /* ---- open the caller-provided path -------------------------------- */
    check(cplus_violation_log_open(argv[1]) == 0, "open caller path succeeds");

    /* ---- refusals while the log is open -------------------------------- */
    refuse_api("event_null_kind",
               cplus_violation_log_event(901, NULL, (void *)(uintptr_t)2, 2),
               EINVAL);
    refuse_api("event_empty_kind",
               cplus_violation_log_event(902, "", (void *)(uintptr_t)3, 3),
               EINVAL);

    memset(oversized, 'K', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    if (strlen(oversized) > AUDIT_KIND_CAP) {
        refuse_policy("event_oversized_kind", strlen(oversized),
                      (size_t)AUDIT_KIND_CAP);
    } else {
        ++g_scenarios;
        ++g_failed;
        printf("FAIL: oversized kind fixture is not actually oversized\n");
    }
    check(cplus_violation_log_count() == 0UL,
          "no refused event was counted before the audit sequence");

    /* ---- fixed, deterministic audit sequence -------------------------- */
    for (i = 0; i < EVENT_COUNT; ++i) {
        char what[64];
        snprintf(what, sizeof(what), "audit event %d accepted",
                 g_events[i].code);
        check(emit(&g_events[i]) == 0, what);
    }
    expected_count = (unsigned long)EVENT_COUNT;
    check(cplus_violation_log_count() == expected_count,
          "count equals number of accepted audit events");

    /* ---- close, then confirm logging is refused again ------------------ */
    cplus_violation_log_close();
    refuse_api("event_after_close",
               cplus_violation_log_event(903, "closed", (void *)(uintptr_t)4, 4),
               EBADF);

    /* ---- append: re-open the SAME path, add one record ----------------- */
    check(cplus_violation_log_open(argv[1]) == 0, "re-open caller path succeeds");
    check(emit(&g_append) == 0, "append event accepted");
    expected_count = (unsigned long)(EVENT_COUNT + 1);
    check(cplus_violation_log_count() == expected_count,
          "cumulative count includes the appended event");
    cplus_violation_log_close();

    /* ---- read back: verify all lines, print a fixed subset ------------- */
    n = read_all(argv[1], buf, sizeof(buf));
    check(n != (size_t)-1, "log file is readable");
    check(count_lines(buf) == EVENT_COUNT + 1,
          "log file has exactly the expected number of lines");

    for (i = 0; i < EVENT_COUNT + 1; ++i) {
        char what[64];
        line[0] = '\0';
        (void)get_line(buf, i, line, sizeof(line));
        snprintf(what, sizeof(what), "line %lu content exact",
                 (unsigned long)i);
        check(strcmp(line, g_expected[i]) == 0, what);
    }

    /* Fixed subset: first two records and the appended last record. */
    {
        const size_t subset[] = {0, 1, EVENT_COUNT};
        for (i = 0; i < sizeof(subset) / sizeof(subset[0]); ++i) {
            line[0] = '\0';
            (void)get_line(buf, subset[i], line, sizeof(line));
            printf("READBACK idx=%lu line=%s\n", (unsigned long)subset[i], line);
        }
    }

    printf("RESULT %d %d %d %d\n", g_scenarios, g_passed, g_failed, g_refused);
    return (g_failed == 0) ? 0 : 1;
}
