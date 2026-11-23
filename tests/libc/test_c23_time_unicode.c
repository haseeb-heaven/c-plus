/*
 * test_c23_time_unicode.c -- deterministic tests for the three C23 APIs added
 * in libc/src/stdlib/c23_time_unicode.c:
 *
 *   timespec_getres  (<time.h>)
 *   mbrtoc8 / c8rtomb (<uchar.h>)
 *
 * Build (standalone, this host):
 *   /usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
 *     -Ilibc/install-include -Ilibc \
 *     libc/src/stdlib/c23_time_unicode.c libc/log/log.c \
 *     tests/libc/test_c23_time_unicode.c -o /tmp/test_c23_time_unicode
 *   timeout 30 /tmp/test_c23_time_unicode
 *
 * The test prints exactly:  c23_time_unicode: <checks> <failures>
 * and returns nonzero when <failures> is nonzero. Every check is deterministic
 * (no timing assertions beyond "resolution is a positive sub-second value",
 * and an exact {0, 1000} assertion on this probe-verified host).
 *
 * A UTF-8 locale is required for the multibyte cases; setlocale is verified
 * and a clear note is printed (and those cases skipped) if it is unavailable.
 */
/* No feature macros: on this SDK <time.h> exposes TIME_UTC under plain
 * -std=c99, and defining _POSIX_C_SOURCE would hide it. */

#include "cplus_c23_time_unicode.h"
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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

/* ---- timespec_getres ----------------------------------------------------- */
static void test_timespec_getres(void) {
    struct timespec ts;
    int base;

    memset(&ts, 0, sizeof ts);
    errno = 0;
    base = cplus_timespec_getres(&ts, TIME_UTC);
    CHECK(base == TIME_UTC);
    CHECK(errno == 0);
    CHECK(ts.tv_sec == 0);
    CHECK(ts.tv_nsec == 1000L); /* probe-verified on this host */
    CHECK(ts.tv_nsec > 0 && ts.tv_nsec < 1000000000L);

    errno = 0;
    CHECK(cplus_timespec_getres(NULL, TIME_UTC) == 0);
    CHECK(errno == EINVAL);

    errno = 0;
    CHECK(cplus_timespec_getres(&ts, 12345) == 0);
    CHECK(errno == EINVAL);
}

/* ---- mbrtoc8 ------------------------------------------------------------- */
static void test_mbrtoc8_ascii_and_null(void) {
    mbstate_t st;
    char8_t c8;

    memset(&st, 0, sizeof st);
    c8 = (char8_t)0xFF;
    errno = 0;
    CHECK(cplus_mbrtoc8(&c8, "A", 1, &st) == 1u);
    CHECK(c8 == (char8_t)'A');
    CHECK(errno == 0);

    /* Null character resets and yields 0. */
    memset(&st, 0, sizeof st);
    c8 = (char8_t)0xFF;
    CHECK(cplus_mbrtoc8(&c8, "", 1, &st) == 0u);
    CHECK(c8 == (char8_t)0);

    /* NULL pc8 is a length-only query. */
    memset(&st, 0, sizeof st);
    CHECK(cplus_mbrtoc8(NULL, "Z", 1, &st) == 1u);

    /* n == 0 cannot complete a character. */
    memset(&st, 0, sizeof st);
    CHECK(cplus_mbrtoc8(&c8, "A", 0, &st) == (size_t)-2);
}

static void test_mbrtoc8_invalid(void) {
    mbstate_t st;
    char8_t c8;

    memset(&st, 0, sizeof st);
    c8 = 0;
    errno = 0;
    CHECK(cplus_mbrtoc8(&c8, "\xFF", 1, &st) == (size_t)-1);
    CHECK(errno == EILSEQ);

    /* Encoded surrogate U+D800 (ED A0 80) is invalid UTF-8. */
    memset(&st, 0, sizeof st);
    errno = 0;
    CHECK(cplus_mbrtoc8(&c8, "\xED\xA0\x80", 3, &st) == (size_t)-1);
    CHECK(errno == EILSEQ);
}

/* 2-byte: C3 A9 == U+00E9. First call with n=1 is incomplete; the completing
 * byte arrives on the next call; the buffered second code unit is returned by
 * a (size_t)-3 call that consumes no input. */
static void test_mbrtoc8_2byte(void) {
    mbstate_t st;
    char8_t c8;
    const char *s = "\xC3\xA9";

    memset(&st, 0, sizeof st);
    c8 = 0;
    CHECK(cplus_mbrtoc8(&c8, s, 1, &st) == (size_t)-2);

    c8 = 0;
    CHECK(cplus_mbrtoc8(&c8, s + 1, 1, &st) == 1u); /* bytes from this s */
    CHECK(c8 == (char8_t)0xC3u);

    c8 = 0;
    CHECK(cplus_mbrtoc8(&c8, NULL, 0, &st) == (size_t)-3);
    CHECK(c8 == (char8_t)0xA9u);

    /* Buffered output drained: normal input is consumed again. */
    c8 = 0;
    CHECK(cplus_mbrtoc8(&c8, "B", 1, &st) == 1u);
    CHECK(c8 == (char8_t)'B');
}

/* 3-byte: E2 82 AC == U+20AC (euro sign), supplied all at once. */
static void test_mbrtoc8_3byte(void) {
    mbstate_t st;
    char8_t c8;
    const char *s = "\xE2\x82\xAC";

    memset(&st, 0, sizeof st);
    c8 = 0;
    CHECK(cplus_mbrtoc8(&c8, s, 3, &st) == 3u);
    CHECK(c8 == (char8_t)0xE2u);
    CHECK(cplus_mbrtoc8(&c8, NULL, 0, &st) == (size_t)-3);
    CHECK(c8 == (char8_t)0x82u);
    CHECK(cplus_mbrtoc8(&c8, NULL, 0, &st) == (size_t)-3);
    CHECK(c8 == (char8_t)0xACu);
    CHECK(cplus_mbrtoc8(&c8, "A", 1, &st) == 1u);
    CHECK(c8 == (char8_t)'A');
}

/* 4-byte: F0 9F 98 80 == U+1F600 (grinning face), fed one byte at a time. */
static void test_mbrtoc8_4byte(void) {
    mbstate_t st;
    char8_t c8;
    const char *s = "\xF0\x9F\x98\x80";
    size_t i;

    memset(&st, 0, sizeof st);
    for (i = 0; i < 3u; ++i) {
        CHECK(cplus_mbrtoc8(&c8, s + i, 1, &st) == (size_t)-2);
    }
    CHECK(cplus_mbrtoc8(&c8, s + 3, 1, &st) == 1u);
    CHECK(c8 == (char8_t)0xF0u);
    CHECK(cplus_mbrtoc8(&c8, NULL, 0, &st) == (size_t)-3);
    CHECK(c8 == (char8_t)0x9Fu);
    CHECK(cplus_mbrtoc8(&c8, NULL, 0, &st) == (size_t)-3);
    CHECK(c8 == (char8_t)0x98u);
    CHECK(cplus_mbrtoc8(&c8, NULL, 0, &st) == (size_t)-3);
    CHECK(c8 == (char8_t)0x80u);
}

/* ---- c8rtomb ------------------------------------------------------------- */
static void test_c8rtomb_ascii_and_null(void) {
    mbstate_t st;
    char out[8];

    memset(&st, 0, sizeof st);
    memset(out, 0, sizeof out);
    errno = 0;
    CHECK(cplus_c8rtomb(out, (char8_t)'A', &st) == 1u);
    CHECK(out[0] == 'A');
    CHECK(errno == 0);

    memset(&st, 0, sizeof st);
    memset(out, 0x7F, sizeof out);
    CHECK(cplus_c8rtomb(out, (char8_t)0, &st) == 1u);
    CHECK(out[0] == '\0');

    /* s == NULL is a length/reset query and must not fault. */
    memset(&st, 0, sizeof st);
    CHECK(cplus_c8rtomb(NULL, (char8_t)'A', &st) == 1u);
    memset(&st, 0, sizeof st);
    CHECK(cplus_c8rtomb(NULL, (char8_t)0, &st) == 1u);
}

static void test_c8rtomb_invalid(void) {
    mbstate_t st;
    char out[8];

    memset(&st, 0, sizeof st);
    errno = 0;
    CHECK(cplus_c8rtomb(out, (char8_t)0xFFu, &st) == (size_t)-1);
    CHECK(errno == EILSEQ);
}

static void test_c8rtomb_multibyte(void) {
    mbstate_t st;
    char out[8];

    /* 2-byte U+00E9. */
    memset(&st, 0, sizeof st);
    memset(out, 0, sizeof out);
    CHECK(cplus_c8rtomb(out, (char8_t)0xC3u, &st) == (size_t)-2);
    CHECK(cplus_c8rtomb(out, (char8_t)0xA9u, &st) == 2u);
    CHECK((unsigned char)out[0] == 0xC3u);
    CHECK((unsigned char)out[1] == 0xA9u);

    /* 3-byte U+20AC. */
    memset(&st, 0, sizeof st);
    memset(out, 0, sizeof out);
    CHECK(cplus_c8rtomb(out, (char8_t)0xE2u, &st) == (size_t)-2);
    CHECK(cplus_c8rtomb(out, (char8_t)0x82u, &st) == (size_t)-2);
    CHECK(cplus_c8rtomb(out, (char8_t)0xACu, &st) == 3u);
    CHECK((unsigned char)out[0] == 0xE2u);
    CHECK((unsigned char)out[1] == 0x82u);
    CHECK((unsigned char)out[2] == 0xACu);

    /* 4-byte U+1F600. */
    memset(&st, 0, sizeof st);
    memset(out, 0, sizeof out);
    CHECK(cplus_c8rtomb(out, (char8_t)0xF0u, &st) == (size_t)-2);
    CHECK(cplus_c8rtomb(out, (char8_t)0x9Fu, &st) == (size_t)-2);
    CHECK(cplus_c8rtomb(out, (char8_t)0x98u, &st) == (size_t)-2);
    CHECK(cplus_c8rtomb(out, (char8_t)0x80u, &st) == 4u);
    CHECK((unsigned char)out[0] == 0xF0u);
    CHECK((unsigned char)out[1] == 0x9Fu);
    CHECK((unsigned char)out[2] == 0x98u);
    CHECK((unsigned char)out[3] == 0x80u);
}

/* ---- full c8rtomb -> mbrtoc8 roundtrip ----------------------------------- */
static void test_roundtrip(const char *bytes, size_t len) {
    char encoded[8];
    char8_t decoded[8];
    mbstate_t enc, dec;
    size_t i, pos = 0;
    size_t ecount;

    memset(&enc, 0, sizeof enc);
    memset(&dec, 0, sizeof dec);
    memset(encoded, 0, sizeof encoded);
    memset(decoded, 0, sizeof decoded);

    /* Encode: feed each source UTF-8 code unit to c8rtomb. */
    for (i = 0; i < len; ++i) {
        size_t r = cplus_c8rtomb(encoded + pos, (char8_t)(unsigned char)bytes[i],
                                 &enc);
        if (r == (size_t)-2 || r == 0u) continue;
        CHECK(r != (size_t)-1);
        pos += r;
    }
    CHECK(pos == len);
    CHECK(memcmp(encoded, bytes, len) == 0);

    /* Decode: feed the multibyte string to mbrtoc8, collecting every UTF-8
     * code unit, including the (size_t)-3 buffered ones. Input position and
     * output position advance independently: -3 consumes no input. */
    ecount = 0;
    {
        size_t ipos = 0u, opos = 0u;
        while (opos < len && ecount < 64u) {
            size_t avail = (ipos < len) ? (len - ipos) : 0u;
            size_t r = cplus_mbrtoc8(&decoded[opos], encoded + ipos, avail,
                                     &dec);
            ++ecount;
            if (r == (size_t)-3 || r == 0u) {
                ++opos;
                continue;
            }
            if (r == (size_t)-2) {
                if (ipos < len) ++ipos; /* offer one more byte */
                else break;
                continue;
            }
            CHECK(r != (size_t)-1);
            if (r == (size_t)-1) break;
            ++opos;      /* first code unit delivered via *pc8 */
            ipos += r;   /* bytes consumed from the input */
        }
        CHECK(opos == len);
    }
    CHECK(memcmp(decoded, bytes, len) == 0);
}

/* ---- state reset and multiple characters in one call --------------------- */
static void test_state_reset_and_multi(void) {
    mbstate_t st;
    char8_t c8;
    const char *two = "AB";

    /* Two complete ASCII characters: each call consumes exactly one byte. */
    memset(&st, 0, sizeof st);
    CHECK(cplus_mbrtoc8(&c8, two, 2, &st) == 1u);
    CHECK(c8 == (char8_t)'A');
    CHECK(cplus_mbrtoc8(&c8, two + 1, 1, &st) == 1u);
    CHECK(c8 == (char8_t)'B');

    /* Zeroing the opaque state mid-partial-sequence resets it: the new call
     * treats its byte as a fresh sequence start. */
    memset(&st, 0, sizeof st);
    CHECK(cplus_mbrtoc8(&c8, "\xE2", 1, &st) == (size_t)-2);
    memset(&st, 0, sizeof st);
    CHECK(cplus_mbrtoc8(&c8, "A", 1, &st) == 1u);
    CHECK(c8 == (char8_t)'A');
}

/* ---- ps == NULL (dedicated internal state) ------------------------------- */
static void test_null_state(void) {
    char8_t c8;
    char out[8];

    memset(out, 0, sizeof out);
    CHECK(cplus_mbrtoc8(&c8, "Q", 1, NULL) == 1u);
    CHECK(c8 == (char8_t)'Q');
    CHECK(cplus_c8rtomb(out, (char8_t)'Q', NULL) == 1u);
    CHECK(out[0] == 'Q');
}

int main(void) {
    int utf8 = 0;

    if (setlocale(LC_ALL, "en_US.UTF-8") != NULL) {
        utf8 = 1;
    } else {
        printf("note: en_US.UTF-8 unavailable; multibyte cases skipped\n");
    }

    test_timespec_getres();
    test_mbrtoc8_ascii_and_null();
    test_mbrtoc8_invalid();
    test_state_reset_and_multi();
    test_null_state();
    test_c8rtomb_ascii_and_null();
    test_c8rtomb_invalid();

    if (utf8) {
        test_mbrtoc8_2byte();
        test_mbrtoc8_3byte();
        test_mbrtoc8_4byte();
        test_c8rtomb_multibyte();
        test_roundtrip("\xC3\xA9", 2u);
        test_roundtrip("\xE2\x82\xAC", 3u);
        test_roundtrip("\xF0\x9F\x98\x80", 4u);
    }

    printf("c23_time_unicode: %d %d\n", checks, failures);
    return failures ? 1 : 0;
}
