/* F-096 -- deterministic fuzz configuration: time.hp ISO-8601 formatting.
 *
 * Fixed-seed LCG plus a hard-coded table of fixed timestamps.  No wall clock
 * value ever enters the digest: time_format_iso8601() is called only with
 * explicit unix_ns values, and the oracle is an independent gmtime_r/strftime
 * call on the same value.  Boundary coverage: sub-second truncation, leap day,
 * 2038, year 2100/2262, buffer sizes 0..24/32/64, NULL buffer, and a
 * deliberately short buffer (documented BOUNDS failure).
 *
 * The `secs_u > LONG_MAX` branch of time_format_iso8601 is unreachable on a
 * 64-bit time_t host (ULLONG_MAX/1e9 < LONG_MAX); it is recorded as a SKIP
 * with that reason and contributes a constant to the digest so the oracle is
 * host-independent.
 *
 * Bounded CASES loop, SIGALRM watchdog, external timeout 30s.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/time.hp"

#define CASES 400
#define FNV_PRIME 1099511628211ULL

static unsigned long long rng_state = 0xF0961234ULL;
static unsigned long long digest = 0xcbf29ce484222325ULL;
static int failures = 0;

static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

static void mix(unsigned long long v) {
    digest = (digest ^ v) * FNV_PRIME;
}

static void invariant(const char *tag) {
    failures++;
    print("INVARIANT", tag);
}

static void on_alarm(int sig) {
    static const char msg[] = "F096 watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

/* Independent oracle: same contract, separate code path. */
static int oracle_iso(unsigned long long ns, char *buf, size_t cap) {
    time_t secs = (time_t)(ns / 1000000000ULL);
    struct tm parts;
    if (gmtime_r(&secs, &parts) == NULL)
        return 0;
    if (strftime(buf, cap, "%Y-%m-%dT%H:%M:%SZ", &parts) == 0)
        return 0;
    return 1;
}

static void fold_text(const char *text) {
    size_t i;
    for (i = 0; text[i] != '\0'; i++)
        mix((unsigned char)text[i]);
    mix(0);
}

/* Fixed timestamp with a hand-computed, hard-coded expected string. */
static void fixed_case(const char *tag, unsigned long long ns,
                       const char *expected) {
    char buf[64];
    error err = error();
    int rc = time_format_iso8601(ns, buf, sizeof(buf), &err);
    if (rc != STDLIB_OK || !err.ok() || strcmp(buf, expected) != 0) {
        print("INVARIANT", tag);
        failures++;
    }
    mix((unsigned long long)rc);
    mix((unsigned long long)err.category());
    mix((unsigned long long)strlen(buf));
    fold_text(expected);
}

/* Exact buffer-size boundary around the 20-byte ISO string + NUL. */
static void size_case(int size, unsigned long long ns, const char *expected) {
    char buf[64];
    error err = error();
    int rc;
    if (size == 0)
        rc = time_format_iso8601(ns, NULL, 0, &err);
    else
        rc = time_format_iso8601(ns, buf, (size_t)size, &err);
    if (size == 0) {
        if (rc != STDLIB_FAIL ||
            err.category() != STDLIB_CATEGORY_INVALID)
            invariant("f096_size_null");
    } else if (size < 21) {
        if (rc != STDLIB_FAIL ||
            err.category() != STDLIB_CATEGORY_BOUNDS || buf[0] != '\0')
            invariant("f096_size_short");
    } else {
        if (rc != STDLIB_OK || !err.ok() || strcmp(buf, expected) != 0)
            invariant("f096_size_fit");
    }
    mix((unsigned long long)size);
    mix((unsigned long long)rc);
    mix((unsigned long long)err.category());
}

int main(void) {
    char buf[64];
    char ref[64];
    char hex[32];
    error err = error();
    int i;
    int rc;
    int size;
    unsigned long long ns;
    unsigned long long secs;
    unsigned long long frac;

    (void)!signal(SIGALRM, on_alarm);
    (void)!alarm(20);

    /* ---- fixed timestamps (no wall clock, no locale, UTC only) --------- */
    fixed_case("f096_epoch", 0ULL, "1970-01-01T00:00:00Z");
    fixed_case("f096_one_ns", 1ULL, "1970-01-01T00:00:00Z");
    fixed_case("f096_subsecond", 999999999ULL, "1970-01-01T00:00:00Z");
    fixed_case("f096_one_sec", 1000000000ULL, "1970-01-01T00:00:01Z");
    fixed_case("f096_leap_day", 951782400000000000ULL,
               "2000-02-29T00:00:00Z");
    fixed_case("f096_y2k", 978307200000000000ULL, "2001-01-01T00:00:00Z");
    fixed_case("f096_unix_sample", 1234567890000000000ULL,
               "2009-02-13T23:31:30Z");
    fixed_case("f096_y2038", 2147483647000000000ULL,
               "2038-01-19T03:14:07Z");
    fixed_case("f096_y2100", 4102444800000000000ULL,
               "2100-01-01T00:00:00Z");
    fixed_case("f096_fraction_trunc", 1580000000123456789ULL,
               "2020-01-26T00:53:20Z");
    /* Largest convenient nanosecond timestamp that still fits in u64. */
    fixed_case("f096_y2262", 9223372036000000000ULL,
               "2262-04-11T23:47:16Z");

    /* ---- buffer-size boundary sweep on a fixed timestamp --------------- */
    for (size = 0; size <= 24; size++)
        size_case(size, 1234567890000000000ULL, "2009-02-13T23:31:30Z");
    size_case(32, 1234567890000000000ULL, "2009-02-13T23:31:30Z");
    size_case(64, 1234567890000000000ULL, "2009-02-13T23:31:30Z");

    /* NULL buffer with a non-zero capacity is the same INVALID path. */
    err.clear();
    if (time_format_iso8601(0ULL, NULL, 64, &err) != STDLIB_FAIL ||
        err.category() != STDLIB_CATEGORY_INVALID)
        invariant("f096_null_buffer");
    mix((unsigned long long)err.category());

    /* Documented SKIP: the range branch needs secs_u > LONG_MAX, impossible
     * when time_t is 64-bit because ULLONG_MAX/1e9 < LONG_MAX. */
    print("SKIP_REASON", "f096_secs_gt_long_max",
          "unreachable on 64-bit time_t (ULLONG_MAX/1e9 < LONG_MAX)");
    mix(0ULL);

    /* ---- seeded random valid timestamps, independent oracle ------------ */
    for (i = 0; i < CASES; i++) {
        secs = (unsigned long long)(rnd() % 4102444801u);
        frac = (unsigned long long)(rnd() % 1000000000u);
        ns = secs * 1000000000ULL + frac;
        size = 21 + (int)(rnd() % 44u);
        err.clear();
        rc = time_format_iso8601(ns, buf, (size_t)size, &err);
        if (oracle_iso(ns, ref, sizeof(ref)) != 1)
            invariant("f096_oracle");
        if (rc != STDLIB_OK || !err.ok() || strcmp(buf, ref) != 0)
            invariant("f096_random_format");
        mix((unsigned long long)i);
        mix((unsigned long long)(ns & 0xffffffffULL));
        mix((unsigned long long)(ns >> 32));
        mix((unsigned long long)size);
        mix((unsigned long long)rc);
        mix((unsigned long long)strlen(buf));
        mix((unsigned char)buf[0]);
        mix((unsigned char)buf[strlen(buf) - 1]);
    }

    (void)!alarm(0);
    mix((unsigned long long)failures);
    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f096_digest", hex);
    if (failures == 0)
        print("f096:", CASES, "cases ok");
    else
        print("f096: FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
