/* Standalone conformance smoke test for the installed <uchar.h> compatibility
 * surface. Includes only "cplus_uchar.h" plus standard headers, proving the
 * header is self-contained on a host with no system <uchar.h>.
 *
 * WHAT IS TESTED HERE
 *   - char16_t / char32_t typedefs (width >= 16 / >= 32 bits, unsigned)
 *   - char8_t typedef when compiled as C23 (width 1, unsigned)
 *   - mbstate_t availability through the header, and zero-width reset shape
 *   - include-guard idempotence
 *   - copied musl UTF-16/UTF-32 conversions (ASCII, explicit states)
 *   - F2 REGRESSION (AUDIT_lifecycle.md): the four conversions must use a
 *     real mbstate_t for host mbrtowc/wcrtomb calls, with the musl surrogate
 *     word kept in separate storage. Covers:
 *       * ASCII conversion through the NULL internal state (ps == NULL);
 *       * a U+1F600 surrogate-pair encode/decode round-trip through the
 *         NULL internal state (skipped when the host locale is not UTF-8);
 *       * zero-width reset / length-only queries;
 *       * canary guards around the file-static host state, plus a
 *         canary-padded caller mbstate_t driven through a split (incomplete)
 *         UTF-8 sequence so the host actually writes conversion state.
 *     The canary assertions are the runtime overflow check; building this
 *     test with -fsanitize=undefined (and the ASan library variant where
 *     available) adds direct OOB diagnostics on top of them.
 *
 * The C23 UTF-8 variants (cplus_mbrtoc8/cplus_c8rtomb) remain untested here:
 * the host exports none of them and no musl source at the pinned commit
 * provides them (see tests/libc/test_uchar_c23.c for the exact blocker).
 */
#include <assert.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> /* setlocale, MB_CUR_MAX */
#include <string.h>
#include <wchar.h>

#include "cplus_uchar.h"
#include "cplus_uchar.h" /* include guard must make this a no-op */

/* Regression diagnostic from libc/src/uchar/uchar.c (not public <uchar.h>). */
extern int cplus_uchar_internal_canary_ok(void);
extern void cplus_libc_log_set_enabled(int enabled);

int main(void) {
    mbstate_t st;
    const char *loc = setlocale(LC_CTYPE, "");
    const int utf8_ok = (loc != 0 && MB_CUR_MAX >= 4);

    cplus_libc_log_set_enabled(0); /* keep assertions readable */

    /* --- C11 UTF-16 / UTF-32 character types ------------------------------ */
    assert(sizeof(char16_t) >= 2u);
    assert(sizeof(char32_t) >= 4u);

    /* uint_leastN_t must be unsigned: -1 retains the top bit. Comparing
     * against 0x7FFF / 0x7FFFFFFF distinguishes unsigned from signed. */
    {
        char16_t c16 = (char16_t)-1;
        char32_t c32 = (char32_t)-1;
        assert(c16 > (char16_t)0x7FFF);
        assert(c32 > (char32_t)0x7FFFFFFF);
    }

    /* Round-trip an ASCII code point through each typedef (type-level only;
     * no multibyte conversion is implied or performed). */
    {
        char16_t c16 = (char16_t)'A';
        char32_t c32 = (char32_t)'A';
        assert(c16 == (char16_t)0x0041);
        assert(c32 == (char32_t)0x00000041);
    }

    /* --- C23 char8_t (header publishes CPLUS_UCHAR_HAS_CHAR8_T when present) -- */
#if defined(CPLUS_UCHAR_HAS_CHAR8_T)
    {
        char8_t c8 = (char8_t)-1;
        assert(sizeof(char8_t) == 1u);
        assert(c8 == (char8_t)255);
        assert((char8_t)'A' == (char8_t)0x41);
    }
#endif

    /* --- mbstate_t flows through the header (zero-width reset shape) ------ */
    memset(&st, 0, sizeof st);
    assert(sizeof(st) > 0u);

    /* --- copied musl UTF-16/UTF-32 conversions (explicit states) ---------- */
    {
        char out[16];
        char16_t c16 = 0;
        char32_t c32 = 0;
        mbstate_t s16, s32;
        memset(&s16, 0, sizeof s16);
        memset(&s32, 0, sizeof s32);
        assert(cplus_mbrtoc16(&c16, "A", 1, &s16) == 1);
        assert(c16 == (char16_t)'A');
        assert(cplus_c16rtomb(out, c16, &s16) == 1);
        assert(out[0] == 'A');
        assert(cplus_mbrtoc32(&c32, "Z", 1, &s32) == 1);
        assert(c32 == (char32_t)'Z');
        assert(cplus_c32rtomb(out, c32, &s32) == 1);
        assert(out[0] == 'Z');
        /* length-only query with NULL destination */
        memset(&s16, 0, sizeof s16);
        assert(cplus_mbrtoc16(NULL, "A", 1, &s16) == 1);
    }

    /* --- F2 regression: NULL state uses a real mbstate_t ------------------ */
    {
        char out[16];
        char16_t c16 = 0;
        char32_t c32 = 0;

        assert(cplus_uchar_internal_canary_ok() == 1);

        /* All four conversions with ps == NULL (the internal static state).
         * Before the fix mbrtoc16/mbrtoc32 handed a 4-byte unsigned to the
         * host as mbstate_t*; on this host that object is 128 bytes. */
        assert(cplus_mbrtoc16(&c16, "A", 1, NULL) == 1);
        assert(c16 == (char16_t)'A');
        memset(out, 0, sizeof out);
        assert(cplus_c16rtomb(out, c16, NULL) == 1);
        assert(out[0] == 'A');
        assert(cplus_mbrtoc32(&c32, "Z", 1, NULL) == 1);
        assert(c32 == (char32_t)'Z');
        memset(out, 0, sizeof out);
        assert(cplus_c32rtomb(out, c32, NULL) == 1);
        assert(out[0] == 'Z');

        assert(cplus_uchar_internal_canary_ok() == 1);
    }

    /* --- F2 regression: zero-width reset / length-only queries ------------ */
    {
        char16_t c16 = (char16_t)0xFFFFu;
        char32_t c32 = (char32_t)0xFFFFFFFFu;
        char out[16];

        /* Empty input is the null wide character, not an error. */
        assert(cplus_mbrtoc16(&c16, "", 1, NULL) == 0);
        assert(c16 == (char16_t)0);
        assert(cplus_mbrtoc32(&c32, "", 1, NULL) == 0);
        assert(c32 == (char32_t)0);

        /* NULL destination is a length-only query. */
        assert(cplus_mbrtoc16(NULL, "A", 1, NULL) == 1);
        assert(cplus_mbrtoc32(NULL, "A", 1, NULL) == 1);

        /* s == NULL is a pure reset/length query for the encoders. */
        assert(cplus_c16rtomb(NULL, 0, NULL) == 1);
        assert(cplus_c32rtomb(NULL, 0, NULL) == 1);

        /* Caller-provided state is also resettable via s == "". */
        memset(&st, 0, sizeof st);
        memset(out, 0, sizeof out);
        assert(cplus_c32rtomb(out, 0, &st) == 1);
        assert(out[0] == '\0');

        assert(cplus_uchar_internal_canary_ok() == 1);
    }

    /* --- F2 regression: U+1F600 surrogate pair round-trip via NULL state -- */
    /* "if host locale supports": UTF-8 requires MB_CUR_MAX >= 4. */
    {
        if (utf8_ok) {
            char out[16];
            char16_t hi = 0, lo = 0;
            size_t r;

            memset(out, 0, sizeof out);
            /* Encoder: high surrogate stores 0xD83D, emits nothing (0). */
            r = cplus_c16rtomb(out, (char16_t)0xD83D, NULL);
            assert(r == 0);
            /* Low surrogate 0xDE00 completes U+1F600 -> 4 UTF-8 bytes. */
            r = cplus_c16rtomb(out, (char16_t)0xDE00, NULL);
            assert(r == 4);
            assert((unsigned char)out[0] == 0xF0u);
            assert((unsigned char)out[1] == 0x9Fu);
            assert((unsigned char)out[2] == 0x98u);
            assert((unsigned char)out[3] == 0x80u);

            /* Decoder: one mbrtowc call yields the high surrogate and leaves
             * the low surrogate pending for the -3 follow-up call. musl
             * retrieves that pending surrogate with a non-NULL (empty)
             * source; passing s == NULL is a reset and discards it. */
            r = cplus_mbrtoc16(&hi, out, 4, NULL);
            assert(r == 4);
            assert(hi == (char16_t)0xD83D);
            r = cplus_mbrtoc16(&lo, "", 1, NULL);
            assert(r == (size_t)-3);
            assert(lo == (char16_t)0xDE00);

            assert(cplus_uchar_internal_canary_ok() == 1);
        } else {
            printf("uchar: skip U+1F600 surrogate round-trip (no UTF-8 locale)\n");
        }
    }

    /* --- F2 regression: canary around a caller mbstate_t + static state --- */
    if (utf8_ok) {
        struct {
            unsigned char head[16];
            mbstate_t state;
            unsigned char tail[16];
        } pad;
        unsigned char guard[16];
        char16_t c16 = 0;
        size_t r;

        memset(guard, 0xA5u, sizeof guard);
        memset(&pad, 0x5Au, sizeof pad);
        memcpy(pad.head, guard, sizeof guard);
        memcpy(pad.tail, guard, sizeof guard);
        memset(&pad.state, 0, sizeof pad.state);
        /* Guard seed must be non-zero, otherwise a canary check comparing
         * zeros to zeros would be vacuous. */
        assert(guard[0] == 0xA5u && guard[sizeof guard - 1u] == 0xA5u);
        assert(memcmp(pad.head, guard, sizeof guard) == 0);
        assert(memcmp(pad.tail, guard, sizeof guard) == 0);

        /* Split UTF-8 for U+1F600 across two calls: the first call returns
         * -2 and forces the host to write a partial state into pad.state. */
        r = cplus_mbrtoc16(&c16, "\xF0\x9F", 2, &pad.state);
        assert(r == (size_t)-2);
        r = cplus_mbrtoc16(&c16, "\x98\x80", 2, &pad.state);
        /* Hosts differ on whether the continuation call reports bytes
         * consumed in that call (Darwin: 2) or whole-sequence bytes; either
         * way it is a completed character, not an error/incomplete result. */
        assert(r > 0u && r <= 4u);
        assert(c16 == (char16_t)0xD83D);
        r = cplus_mbrtoc16(&c16, "", 1, &pad.state);
        assert(r == (size_t)-3);
        assert(c16 == (char16_t)0xDE00);

        /* Host wrote only within sizeof(mbstate_t); canaries are intact. */
        assert(memcmp(pad.head, guard, sizeof guard) == 0);
        assert(memcmp(pad.tail, guard, sizeof guard) == 0);

        /* Drive the *internal* static state through a split sequence too:
         * this is the exact path that overflowed before the fix, because the
         * 4-byte pseudo-state was passed to mbrtowc as a 128-byte mbstate_t.
         * The first call stores a partial state; the canary must survive it. */
        r = cplus_mbrtoc16(&c16, "\xF0\x9F", 2, NULL);
        assert(r == (size_t)-2);
        assert(cplus_uchar_internal_canary_ok() == 1);
        r = cplus_mbrtoc16(&c16, "\x98\x80", 2, NULL);
        assert(r > 0u && r <= 4u);
        assert(c16 == (char16_t)0xD83D);
        r = cplus_mbrtoc16(&c16, "", 1, NULL);
        assert(r == (size_t)-3);
        assert(c16 == (char16_t)0xDE00);
        assert(cplus_uchar_internal_canary_ok() == 1);
    }

    cplus_libc_log_set_enabled(0);
    puts("uchar: ok");
    return 0;
}
