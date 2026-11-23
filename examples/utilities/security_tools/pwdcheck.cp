/* pwdcheck.cp -- deterministic, bounded password-policy DEMONSTRATION.
 *
 * NOT a production password checker and NOT a login/authentication tool.  All
 * sample passwords are fixed dummy strings; no real secret is read, stored or
 * logged.  Input comes only from fixed argv; the password is never echoed.
 *
 * Build (from repo root):
 *   ./build/c+ build examples/utilities/security_tools/pwdcheck.cp \
 *     --strict --sanitize=none -I. -o examples/utilities/security_tools/pwdcheck \
 *     -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
 *
 * Usage:
 *   pwdcheck <candidate>
 *
 * Rules (fixed): length 8..32 printable ASCII, at least one uppercase, one
 * lowercase, one digit and one symbol from !@#$%^&*()-_=+[]{};:,.<>/?
 * A hostile oversized candidate is detected by cplus_strnlen_s(buf, MAX+1)
 * and REFUSED with CPLUS_EXIT_BOUNDS (101) before it is copied.
 */
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS 1
#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_string.h"

#include <stdio.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"

#define PC_MIN_LEN 8
#define PC_MAX_LEN 32

static int g_failed = 0;

static void mark(const char *label, const char *got, const char *want,
                 const char *detail) {
    printf("PWCHECK %s %s %s\n", label, got, detail);
    if (strcmp(got, want) != 0)
        g_failed = 1;
    fflush(stdout);
}

static int refuse(const char *label, const char *reason, int category,
                  cplus_exit_code code) {
    error e = error();
    e.set_category(category, code, reason);
    printf("PWCHECK %s REFUSED code=%d cat=%d reason=%s\n",
           label, e.code(), e.category(), reason);
    fflush(stdout);
    return 2;
}

static void pc_arg(char **argv, int i, const char **out) {
    const char *v = (const char *)0;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    *out = v;
}

static int pc_upper(unsigned char c) { return c >= (unsigned char)'A' && c <= (unsigned char)'Z'; }
static int pc_lower(unsigned char c) { return c >= (unsigned char)'a' && c <= (unsigned char)'z'; }
static int pc_digit(unsigned char c) { return c >= (unsigned char)'0' && c <= (unsigned char)'9'; }

static int pc_symbol(unsigned char c) {
    static const char syms[] = "!@#$%^&*()-_=+[]{};:,.<>/?";
    size_t i;
    for (i = 0; i < (size_t)(sizeof(syms) - 1u); i++)
        if ((unsigned char)syms[i] == c)
            return 1;
    return 0;
}

/* Rule evaluation over an already-bounded, NUL-terminated local buffer.
 * Returns the number of satisfied character-class rules (0..4). */
static int pc_score(const char *text, size_t n, int *u, int *l, int *d, int *s) {
    size_t i;
    *u = 0; *l = 0; *d = 0; *s = 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x21u || c > 0x7eu)
            return -1;
        if (pc_upper(c)) *u = 1;
        if (pc_lower(c)) *l = 1;
        if (pc_digit(c)) *d = 1;
        if (pc_symbol(c)) *s = 1;
    }
    return *u + *l + *d + *s;
}

int main(int argc, char **argv) {
    static const char good_dummy[] = "Aa1!aaaa";
    static const char weak_dummy[] = "aaaaaaaa";
    char buf[PC_MAX_LEN + 1];
    const char *candidate = (const char *)0;
    rsize_t n = 0;
    int u = 0, l = 0, d = 0, s = 0;
    int score = 0;
    char detail[160];

    /* Self-check with fixed dummy inputs: no real secret is involved. */
    score = pc_score(good_dummy, (size_t)(sizeof(good_dummy) - 1u), &u, &l, &d, &s);
    if (score != 4)
        return refuse("selfcheck", "rule-engine-broken", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);
    score = pc_score(weak_dummy, (size_t)(sizeof(weak_dummy) - 1u), &u, &l, &d, &s);
    if (score != 1)
        return refuse("selfcheck", "rule-engine-broken", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);
    mark("selfcheck", "OK", "OK", "fixed-dummy-rules-ok");

    if (argc < 2)
        return refuse("usage", "missing-input", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);
    if (argc > 2)
        return refuse("too_many_args", "more-than-one-input",
                      STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS);
    pc_arg(argv, 1, &candidate);
    if (candidate == (const char *)0)
        return refuse("null_input", "null-pointer", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_NULL_POINTER);

    /* Hostile oversized input: the bounded query stops at MAX + 1. */
    n = cplus_strnlen_s(candidate, (rsize_t)(PC_MAX_LEN + 1));
    if (n > (rsize_t)PC_MAX_LEN)
        return refuse("oversize", "input-too-long", STDLIB_CATEGORY_BOUNDS,
                      CPLUS_EXIT_BOUNDS);
    if (n < (rsize_t)PC_MIN_LEN)
        return refuse("too_short", "input-too-short", STDLIB_CATEGORY_RANGE,
                      CPLUS_EXIT_BOUNDS);

    buf[0] = '\0';
    {
        errno_t rc = cplus_strncpy_s(buf, (rsize_t)sizeof(buf), candidate, n);
        if (rc != CPLUS_SAFE_OK)
            return refuse("copy", "checked-copy-failed",
                          STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
    }

    score = pc_score(buf, (size_t)n, &u, &l, &d, &s);
    if (score < 0)
        return refuse("bad_byte", "non-printable-input", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);
    if (score != 4)
        return refuse("weak", "policy-rules-not-met", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);

    snprintf(detail, sizeof(detail),
             "len=%u score=%d upper=%d lower=%d digit=%d symbol=%d",
             (unsigned)n, score, u, l, d, s);
    mark("strong", "OK", "OK", detail);
    printf("PWCHECK done\n");
    fflush(stdout);
    return g_failed;
}
