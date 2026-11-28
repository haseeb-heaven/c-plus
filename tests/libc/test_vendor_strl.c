/*
 * test_vendor_strl.c -- standalone C99 test for the verbatim vendored
 * safeclib strlcpy()/strlcat() in libc/src/safecrt_vendor/strl_group.c.
 *
 * Build and run (from the repo root, build/safe/config.h must exist):
 *   sh scripts/build_safecrt.sh >/dev/null 2>&1
 *   /usr/bin/clang -O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 \
 *     -Ithird_party/safeclib/include -Ibuild/safe \
 *     -Ithird_party/safeclib/src -Istdlib/safe -Wno-macro-redefined \
 *     -c libc/src/safecrt_vendor/strl_group.c -o /tmp/strl_group.o
 *   /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
 *     tests/libc/test_vendor_strl.c /tmp/strl_group.o \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -o /tmp/test_vendor_strl
 *   timeout 30 /tmp/test_vendor_strl
 *
 * Prints: vendor_strl: <checks> <failures>   (exit nonzero on failures)
 */
#include "cplus_vendor_strl.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

#define CAP 64

/* --------------------------------------------------------------------
 * Local reference implementation of the documented strlcpy/strlcat
 * semantics (BSD / safeclib headers), written independently of the host
 * libc and of the vendored object under test. Used only as an oracle for
 * return value and destination buffer contents.
 * -------------------------------------------------------------------- */
static size_t ref_strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0')
        ++n;
    return n;
}

static size_t ref_strlcpy(char *dst, const char *src, size_t dsize)
{
    size_t slen = ref_strlen(src);
    if (dsize != 0) {
        size_t n = slen < dsize - 1 ? slen : dsize - 1;
        size_t i;
        for (i = 0; i < n; ++i)
            dst[i] = src[i];
        dst[n] = '\0';
    }
    return slen;
}

static size_t ref_strlcat(char *dst, const char *src, size_t dsize)
{
    size_t dlen = 0;
    size_t slen = ref_strlen(src);
    size_t room;
    size_t n;
    size_t i;

    while (dlen < dsize && dst[dlen] != '\0')
        ++dlen;
    if (dlen == dsize)
        return dlen + slen; /* no room: no write, count the whole source */

    room = dsize - dlen - 1;
    n = slen < room ? slen : room;
    for (i = 0; i < n; ++i)
        dst[dlen + i] = src[i];
    dst[dlen + n] = '\0';
    return dlen + slen;
}

/* Fill a CAP-sized buffer with a non-NUL sentinel so untouched bytes are
 * detectable by memcmp. */
static void fill_sentinel(char *b)
{
    memset(b, 0xA5, CAP);
}

/* -------------------------------------------------------------------- */

static void test_strlcpy_parity(void)
{
    static const char *const srcs[] = {
        "", "a", "abc", "abcdefgh", "0123456789abcdef0123456789"
    };
    size_t si;
    size_t dsize;

    for (si = 0; si < sizeof(srcs) / sizeof(srcs[0]); ++si) {
        for (dsize = 0; dsize <= 40; ++dsize) {
            char got[CAP];
            char want[CAP];
            size_t rg;
            size_t rw;

            fill_sentinel(got);
            fill_sentinel(want);
            rg = cplus_v_strlcpy(got, srcs[si], dsize);
            rw = ref_strlcpy(want, srcs[si], dsize);
            CHECK(rg == rw);
            CHECK(memcmp(got, want, CAP) == 0);
        }
    }
}

static void test_strlcat_parity(void)
{
    static const char *const srcs[] = {
        "", "a", "abc", "abcdefgh", "0123456789abcdef0123456789"
    };
    static const char *const inits[] = { "", "X", "pre", "abcdefgh" };
    size_t si;
    size_t ii;
    size_t dsize;

    for (ii = 0; ii < sizeof(inits) / sizeof(inits[0]); ++ii) {
        for (si = 0; si < sizeof(srcs) / sizeof(srcs[0]); ++si) {
            for (dsize = 0; dsize <= 40; ++dsize) {
                char got[CAP];
                char want[CAP];
                size_t keep = ref_strlen(inits[ii]);
                size_t rg;
                size_t rw;

                fill_sentinel(got);
                fill_sentinel(want);
                /* Establish the precondition: dest NUL-terminated within
                 * the first dsize bytes whenever dsize > 0. */
                if (dsize != 0) {
                    if (keep > dsize - 1)
                        keep = dsize - 1;
                    memcpy(got, inits[ii], keep);
                    got[keep] = '\0';
                    memcpy(want, inits[ii], keep);
                    want[keep] = '\0';
                }

                rg = cplus_v_strlcat(got, srcs[si], dsize);
                rw = ref_strlcat(want, srcs[si], dsize);
                CHECK(rg == rw);
                CHECK(memcmp(got, want, CAP) == 0);
            }
        }
    }
}

static void test_explicit_boundaries(void)
{
    char b[CAP];

    /* strlcpy exact fit: src length == dsize-1 -> no truncation. */
    fill_sentinel(b);
    CHECK(cplus_v_strlcpy(b, "abc", 4) == 3);
    CHECK(strcmp(b, "abc") == 0);

    /* strlcpy truncation: dsize == src length -> NUL replaces last char. */
    fill_sentinel(b);
    CHECK(cplus_v_strlcpy(b, "abcd", 4) == 4);
    CHECK(strcmp(b, "abc") == 0);

    /* strlcpy dsize == 0: no write, full source length reported. */
    fill_sentinel(b);
    CHECK(cplus_v_strlcpy(b, "abcd", 0) == 4);
    CHECK((unsigned char)b[0] == 0xA5);

    /* strlcat exact fit: "ab" + "cd" into dsize 5. */
    fill_sentinel(b);
    strcpy(b, "ab");
    CHECK(cplus_v_strlcat(b, "cd", 5) == 4);
    CHECK(strcmp(b, "abcd") == 0);

    /* strlcat truncation: dsize 4 leaves room for 1 appended char. */
    fill_sentinel(b);
    strcpy(b, "ab");
    CHECK(cplus_v_strlcat(b, "cdef", 4) == 6);
    CHECK(strcmp(b, "abc") == 0);

    /* strlcat with no NUL in the first dsize bytes: upstream returns
     * dsize + strlen(src) and leaves the buffer untouched. */
    fill_sentinel(b);
    memset(b, 'z', CAP);
    CHECK(cplus_v_strlcat(b, "abc", 4) == 7);
    {
        char want[CAP];
        memset(want, 'z', CAP);
        CHECK(memcmp(b, want, CAP) == 0);
    }
}

static void test_null_constraints(void)
{
    char b[CAP];

    /* strlcpy: NULL dest -> 0; NULL src -> 0 and dest[0] = '\0'. */
    CHECK(cplus_v_strlcpy(NULL, "abc", 4) == 0);

    fill_sentinel(b);
    CHECK(cplus_v_strlcpy(b, NULL, 4) == 0);
    CHECK(b[0] == '\0');

    /* NULL src with dsize 0 writes nothing. */
    fill_sentinel(b);
    CHECK(cplus_v_strlcpy(b, NULL, 0) == 0);
    CHECK((unsigned char)b[0] == 0xA5);

    /* strlcat: NULL dest -> 0; NULL src -> strlen(dest), dest unchanged. */
    CHECK(cplus_v_strlcat(NULL, "abc", 4) == 0);

    fill_sentinel(b);
    strcpy(b, "xy");
    CHECK(cplus_v_strlcat(b, NULL, 4) == 2);
    CHECK(strcmp(b, "xy") == 0);
}

int main(void)
{
    test_strlcpy_parity();
    test_strlcat_parity();
    test_explicit_boundaries();
    test_null_constraints();

    printf("vendor_strl: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
