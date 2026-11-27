/*
 * Deterministic regressions for Agent 2 libc issues #173, #174 and #175 in
 * libc/src/stdlib/c23_time_unicode.c.
 *
 *   #173  ps == NULL must not be one process-wide mutable mbstate_t, and a
 *         recycled compact-host pool slot must never silently alias a live
 *         caller's state.
 *   #174  mbrtoc8: pending-unit drain must advance even for pc8 == NULL;
 *         s == NULL is exactly mbrtoc8(NULL, "", 1, ps); a full d_in buffer
 *         must be diagnosed with EILSEQ instead of looping on (size_t)-2.
 *   #175  c8rtomb: incomplete UTF-8 returns 0 (never (size_t)-2); s == NULL
 *         resets; char8_t input is decoded as UTF-8 independent of LC_CTYPE.
 *
 * Built by tests/libc/run_libc_tests.sh like every other test_*.c file, or
 * standalone:
 *   clang -O2 -Wall -Wextra -pedantic -std=c99 -D_GNU_SOURCE \
 *     -Ilibc -Ilibc/include -Ilibc/install-include \
 *     tests/libc/test_c23_time_unicode_issues_173_174_175.c \
 *     build/libc/libcplus_hardened.a -lm -pthread -o /tmp/c23_regress
 *   timeout 60 /tmp/c23_regress
 *
 * Prints exactly:  c23_time_unicode_regressions: <checks> <failures>
 * Multi-byte cases require en_US.UTF-8 and are skipped with a note otherwise.
 */
#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "cplus_c23_time_unicode.h"
#include <errno.h>
#include <locale.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++checks;                                                              \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                      \
    } while (0)

/* ---- #174: pc8 == NULL drain must make progress -------------------------- */
static void test_pc8_null_drain_advances(void) {
    mbstate_t st;
    char8_t c8;
    size_t r;

    memset(&st, 0, sizeof st);
    r = cplus_mbrtoc8(NULL, "\xE2\x82\xAC", 3u, &st); /* U+20AC, 1st unit held back */
    CHECK(r == 3u);
    r = cplus_mbrtoc8(NULL, "", 1u, &st); /* drains 0x82 without storing it */
    CHECK(r == (size_t)-3);
    r = cplus_mbrtoc8(NULL, "", 1u, &st); /* drains 0xAC without storing it */
    CHECK(r == (size_t)-3);

    /* No buffered units left: s == NULL resets and must NOT write through the
     * caller's pc8, even though the caller passed a non-NULL one. */
    c8 = (char8_t)0x7Fu;
    r = cplus_mbrtoc8(&c8, NULL, 0u, &st);
    CHECK(r == 0u);
    CHECK(c8 == (char8_t)0x7Fu);
}

/* ---- #174: s == NULL reset semantics ------------------------------------- */
static void test_s_null_reset(void) {
    mbstate_t st;
    char8_t c8;
    size_t r;

    memset(&st, 0, sizeof st);
    r = cplus_mbrtoc8(&c8, "\xC3\xA9", 2u, &st);
    CHECK(r == 2u);
    CHECK(c8 == (char8_t)0xC3u); /* 0xA9 is buffered */

    /* n is ignored for s == NULL; the call is mbrtoc8(NULL, "", 1, ps). */
    c8 = (char8_t)0x7Fu;
    r = cplus_mbrtoc8(&c8, NULL, 99u, &st);
    CHECK(r == (size_t)-3);
    CHECK(c8 == (char8_t)0x7Fu); /* untouched: pc8 forced to NULL */

    r = cplus_mbrtoc8(&c8, NULL, 0u, &st);
    CHECK(r == 0u);
    CHECK(c8 == (char8_t)0x7Fu);

    c8 = 0u;
    r = cplus_mbrtoc8(&c8, "A", 1u, &st);
    CHECK(r == 1u);
    CHECK(c8 == (char8_t)'A');
}

/* ---- #174: invalid / overlong input is rejected, not looped on ---------- */
static void test_invalid_is_not_endless(void) {
    mbstate_t st;
    char8_t c8;
    size_t r;

    memset(&st, 0, sizeof st);
    errno = 0;
    r = cplus_mbrtoc8(&c8, "\xC3", 1u, &st); /* valid incomplete prefix */
    CHECK(r == (size_t)-2);
    r = cplus_mbrtoc8(&c8, "\xC3", 1u, &st); /* bad continuation -> EILSEQ */
    CHECK(r == (size_t)-1);
    CHECK(errno == EILSEQ);
}

/* ---- #175: incomplete UTF-8 returns 0, s == NULL resets ------------------ */
static void test_c8rtomb_zero_and_reset(void) {
    mbstate_t st;
    char out[8];
    size_t r;

    memset(&st, 0, sizeof st);
    memset(out, 0, sizeof out);
    r = cplus_c8rtomb(out, (char8_t)0xC3u, &st);
    CHECK(r == 0u); /* never (size_t)-2 */
    r = cplus_c8rtomb(out, (char8_t)0xA9u, &st);
    CHECK(r == 2u);
    CHECK((unsigned char)out[0] == 0xC3u);
    CHECK((unsigned char)out[1] == 0xA9u);

    /* Reset a partial sequence with s == NULL (c8 ignored). */
    memset(&st, 0, sizeof st);
    CHECK(cplus_c8rtomb(out, (char8_t)0xE2u, &st) == 0u);
    CHECK(cplus_c8rtomb(NULL, (char8_t)'Z', &st) == 1u);
    memset(out, 0, sizeof out);
    CHECK(cplus_c8rtomb(out, (char8_t)'B', &st) == 1u);
    CHECK(out[0] == 'B');
}

/* ---- #175: char8_t decode must not depend on LC_CTYPE -------------------- */
static void test_c8rtomb_c_locale_input(void) {
    mbstate_t st;
    char out[8];
    size_t r;

    CHECK(setlocale(LC_ALL, "C") != NULL);
    memset(&st, 0, sizeof st);
    memset(out, 0, sizeof out);
    errno = 0;
    r = cplus_c8rtomb(out, (char8_t)0xC3u, &st);
    CHECK(r == 0u); /* accepted as a UTF-8 prefix even in "C" locale */
    CHECK(errno == 0);
}

/* ---- #173: >64 distinct mbstate_t objects must not silently alias ------- */
#define C23_TU_NSTATE 130

static void test_no_silent_pool_alias(void) {
    mbstate_t st[C23_TU_NSTATE];
    unsigned char lead[C23_TU_NSTATE];
    static const unsigned char leads[3] = {0xC3u, 0xE2u, 0xF0u};
    static const unsigned char cont[3][3] = {
        {0xA9u, 0u, 0u}, {0x82u, 0xACu, 0u}, {0x9Fu, 0x98u, 0x80u}};
    static const unsigned char ncont[3] = {1u, 2u, 3u};
    int i;
    unsigned int k, j;
    size_t r;
    char8_t c8;
    char rest[3];

    /* Give each object a DIFFERENT partial prefix so an aliased slot produces
     * a wrong first code unit instead of an indistinguishable one. */
    for (i = 0; i < C23_TU_NSTATE; ++i) {
        k = (unsigned int)(i % 3);
        memset(&st[i], 0, sizeof st[i]);
        lead[i] = leads[k];
        c8 = 0u;
        CHECK(cplus_mbrtoc8(&c8, (const char *)&lead[i], 1u, &st[i]) ==
              (size_t)-2);
    }
    for (i = 0; i < C23_TU_NSTATE; ++i) {
        k = (unsigned int)(i % 3);
        for (j = 0u; j < ncont[k]; ++j) rest[j] = (char)cont[k][j];
        c8 = 0u;
        errno = 0;
        r = cplus_mbrtoc8(&c8, rest, (size_t)ncont[k], &st[i]);
        if (r == (size_t)-1) {
            /* Compact hosts: a recycled slot is now STALE.  The fixed code
             * diagnoses it with EILSEQ; the buggy code silently returned a
             * success carrying another object's bytes. */
            CHECK(errno == EILSEQ);
            continue;
        }
        CHECK(r == (size_t)ncont[k]);
        CHECK(c8 == (char8_t)lead[i]);
    }
}

/* ---- #173: concurrent ps == NULL calls keep independent state ----------- */
typedef struct {
    unsigned char lead;
    unsigned char cont[3];
    unsigned int ncont;
} c23_tu_pattern;

static void *c23_tu_worker(void *arg) {
    c23_tu_pattern *p = (c23_tu_pattern *)arg;
    int iter;
    unsigned int j;
    size_t r;
    char8_t u;

    for (iter = 0; iter < 400; ++iter) {
        r = cplus_mbrtoc8(&u, (const char *)&p->lead, 1u, NULL);
        if (r != (size_t)-2) return (void *)1;
        for (j = 0u; j < p->ncont; ++j) {
            r = cplus_mbrtoc8(&u, (const char *)&p->cont[j], 1u, NULL);
            if (j + 1u < p->ncont) {
                if (r != (size_t)-2) return (void *)1;
            } else {
                if (r != 1u || u != (char8_t)p->lead) return (void *)1;
            }
        }
        for (j = 0u; j < p->ncont; ++j) {
            r = cplus_mbrtoc8(&u, "", 1u, NULL);
            if (r != (size_t)-3 || u != (char8_t)p->cont[j]) return (void *)1;
        }
        r = cplus_mbrtoc8(&u, "", 1u, NULL);
        if (r != 0u) return (void *)1;
    }
    return NULL;
}

static void test_null_state_threads(void) {
    c23_tu_pattern p1 = {0xC3u, {0xA9u, 0u, 0u}, 1u};
    c23_tu_pattern p2 = {0xE2u, {0x82u, 0xACu, 0u}, 2u};
    pthread_t t1, t2;
    void *rv1 = NULL, *rv2 = NULL;

    CHECK(pthread_create(&t1, NULL, c23_tu_worker, &p1) == 0);
    CHECK(pthread_create(&t2, NULL, c23_tu_worker, &p2) == 0);
    CHECK(pthread_join(t1, &rv1) == 0);
    CHECK(pthread_join(t2, &rv2) == 0);
    CHECK(rv1 == NULL);
    CHECK(rv2 == NULL);
}

int main(void) {
    int utf8;

    utf8 = (setlocale(LC_ALL, "en_US.UTF-8") != NULL);
    if (!utf8)
        printf("note: en_US.UTF-8 unavailable; multibyte cases skipped\n");

    if (utf8) {
        test_pc8_null_drain_advances();
        test_s_null_reset();
        test_invalid_is_not_endless();
        test_c8rtomb_zero_and_reset();
        test_no_silent_pool_alias();
        test_null_state_threads();
    }
    /* Must run after the UTF-8 cases: it deliberately switches to "C". */
    test_c8rtomb_c_locale_input();

    printf("c23_time_unicode_regressions: %d %d\n", checks, failures);
    return failures ? 1 : 0;
}
