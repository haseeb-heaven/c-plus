/*
 * Deterministic per-method memory-safety probe: C+ checked format/numeric APIs.
 *
 * Prints one line per case:  <method> <case> <token>
 * Tokens:
 *   OK               operation succeeded, output/canary verified
 *   REFUSED          API rejected the operation (negative return / guard)
 *   TRUNC            API succeeded but the output was bounded/truncated
 *   OVERFLOW_REFUSED numeric conversion signalled a defined overflow
 *   BAD              observed behaviour did not match the modelled expectation
 *
 * The violation trap is disabled so that guarded violations return -1 instead
 * of _exit(70); every case therefore completes and emits exactly one line.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "safe_format.h"
#include "safe_runtime.h"

/* Thin platform-backed numeric wrappers from libcplus_hardened.a.
 * (Declared locally to keep this probe independent of the full cplus_libc.h.) */
extern long cplus_strtol(const char *, char **, int);
extern unsigned long cplus_strtoul(const char *, char **, int);
extern intmax_t cplus_strtoimax(const char *, char **, int);

#define FILL 0xA5u

typedef struct {
    unsigned char pre[4];
    char buf[6];
    unsigned char post[4];
} slot_t;

static void slot_init(slot_t *s) { memset(s, FILL, sizeof(*s)); }

static int slot_canary_ok(const slot_t *s) {
    const unsigned char expect[4] = {FILL, FILL, FILL, FILL};
    return memcmp(s->pre, expect, 4) == 0 && memcmp(s->post, expect, 4) == 0;
}

static void emit(const char *m, const char *c, const char *tok) {
    printf("%s %s %s\n", m, c, tok);
}

/* ---- vsnprintf_s adapter (va_list entry point) ---------------------------- */
static int call_vsnprintf_s(char *d, size_t n, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vsnprintf_s(d, n, fmt, ap);
    va_end(ap);
    return r;
}

/* ---- sprintf_s (checked entry point) -------------------------------------- */
static void test_sprintf_s(void) {
    const char *M = "sprintf_s";
    slot_t s;
    int r;

    slot_init(&s);
    r = cplus_sprintf_s_checked(1, sizeof(s.buf), s.buf, sizeof(s.buf), "AB%d", 123);
    emit(M, "exact_fit", (r == 5 && strcmp(s.buf, "AB123") == 0) ? "OK" : "BAD");

    r = cplus_sprintf_s_checked(1, 6, NULL, 6, "AB%d", 123);
    emit(M, "null_dest", (r < 0) ? "REFUSED" : "BAD");

    /* One-byte-too-small relative to the 5-char payload: dmax=5 leaves room
     * for only 4 chars + NUL, so the checked API bounds (truncates) the write. */
    slot_init(&s);
    r = cplus_sprintf_s_checked(1, sizeof(s.buf), s.buf, 5, "AB%d", 123);
    emit(M, "one_short", (r >= 0 && strcmp(s.buf, "AB12") == 0) ? "TRUNC" : "BAD");

    slot_init(&s);
    {
        int dummy = 0;
        r = cplus_sprintf_s_checked(1, sizeof(s.buf), s.buf, sizeof(s.buf),
                                    "AB%d%n", 123, &dummy);
        emit(M, "pct_n", (r < 0) ? "REFUSED" : "BAD");
    }

    slot_init(&s);
    r = cplus_sprintf_s_checked(1, sizeof(s.buf), s.buf, sizeof(s.buf), "AB%d", 123);
    emit(M, "canary", (r == 5 && slot_canary_ok(&s)) ? "OK" : "BAD");
}

/* ---- snprintf_s (checked entry point) ------------------------------------- */
static void test_snprintf_s(void) {
    const char *M = "snprintf_s";
    slot_t s;
    int r;

    slot_init(&s);
    r = cplus_snprintf_s_checked(1, sizeof(s.buf), s.buf, sizeof(s.buf), "AB%d", 123);
    emit(M, "exact_fit", (r == 5 && strcmp(s.buf, "AB123") == 0) ? "OK" : "BAD");

    r = cplus_snprintf_s_checked(1, 6, NULL, 6, "AB%d", 123);
    emit(M, "null_dest", (r < 0) ? "REFUSED" : "BAD");

    slot_init(&s);
    r = cplus_snprintf_s_checked(1, sizeof(s.buf), s.buf, 5, "AB%d", 123);
    emit(M, "one_short", (r >= 0 && strcmp(s.buf, "AB12") == 0) ? "TRUNC" : "BAD");

    slot_init(&s);
    {
        int dummy = 0;
        r = cplus_snprintf_s_checked(1, sizeof(s.buf), s.buf, sizeof(s.buf),
                                     "AB%d%n", 123, &dummy);
        emit(M, "pct_n", (r < 0) ? "REFUSED" : "BAD");
    }

    slot_init(&s);
    r = cplus_snprintf_s_checked(1, sizeof(s.buf), s.buf, sizeof(s.buf), "AB%d", 123);
    emit(M, "canary", (r == 5 && slot_canary_ok(&s)) ? "OK" : "BAD");
}

/* ---- vsnprintf_s ---------------------------------------------------------- */
static void test_vsnprintf_s(void) {
    const char *M = "vsnprintf_s";
    slot_t s;
    int r;

    slot_init(&s);
    r = call_vsnprintf_s(s.buf, sizeof(s.buf), "AB%d", 123);
    emit(M, "exact_fit", (r == 5 && strcmp(s.buf, "AB123") == 0) ? "OK" : "BAD");

    r = call_vsnprintf_s(NULL, 6, "AB%d", 123);
    emit(M, "null_dest", (r < 0) ? "REFUSED" : "BAD");

    slot_init(&s);
    r = call_vsnprintf_s(s.buf, 5, "AB%d", 123);
    emit(M, "one_short", (r >= 0 && strcmp(s.buf, "AB12") == 0) ? "TRUNC" : "BAD");

    slot_init(&s);
    {
        int dummy = 0;
        r = call_vsnprintf_s(s.buf, sizeof(s.buf), "AB%d%n", 123, &dummy);
        emit(M, "pct_n", (r < 0) ? "REFUSED" : "BAD");
    }

    slot_init(&s);
    r = call_vsnprintf_s(s.buf, sizeof(s.buf), "AB%d", 123);
    emit(M, "canary", (r == 5 && slot_canary_ok(&s)) ? "OK" : "BAD");
}

/* ---- numeric conversions (defined ERANGE refusal) ------------------------- */
#define OVERFLOW_STR "999999999999999999999999999999999"

static void test_strtol(void) {
    const char *M = "strtol";
    char *end = NULL;
    long v;
    errno = 0;
    v = cplus_strtol("12345", &end, 10);
    emit(M, "valid", (errno == 0 && v == 12345L && end && *end == '\0') ? "OK" : "BAD");
    errno = 0;
    v = cplus_strtol(OVERFLOW_STR, &end, 10);
    emit(M, "overflow", (errno == ERANGE) ? "OVERFLOW_REFUSED" : "BAD");
}

static void test_strtoul(void) {
    const char *M = "strtoul";
    char *end = NULL;
    unsigned long v;
    errno = 0;
    v = cplus_strtoul("12345", &end, 10);
    emit(M, "valid", (errno == 0 && v == 12345UL && end && *end == '\0') ? "OK" : "BAD");
    errno = 0;
    v = cplus_strtoul(OVERFLOW_STR, &end, 10);
    emit(M, "overflow", (errno == ERANGE) ? "OVERFLOW_REFUSED" : "BAD");
}

static void test_strtoimax(void) {
    const char *M = "strtoimax";
    char *end = NULL;
    intmax_t v;
    errno = 0;
    v = cplus_strtoimax("12345", &end, 10);
    emit(M, "valid", (errno == 0 && v == (intmax_t)12345 && end && *end == '\0') ? "OK" : "BAD");
    errno = 0;
    v = cplus_strtoimax(OVERFLOW_STR, &end, 10);
    emit(M, "overflow", (errno == ERANGE) ? "OVERFLOW_REFUSED" : "BAD");
}

int main(void) {
    /* Guarded violations must return -1 rather than _exit(70), so every case
     * can emit its token deterministically. */
    cplus_runtime_set_violation_trap(0);

    test_sprintf_s();
    test_snprintf_s();
    test_vsnprintf_s();
    test_strtol();
    test_strtoul();
    test_strtoimax();
    return 0;
}
