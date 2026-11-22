/* Standalone conformance smoke test for the installed <uchar.h> compatibility
 * surface. Includes only "cplus_uchar.h" plus standard headers, proving the
 * header is self-contained on a host with no system <uchar.h>.
 *
 * WHAT IS TESTED HERE
 *   - char16_t / char32_t typedefs (width >= 16 / >= 32 bits, unsigned)
 *   - char8_t typedef when compiled as C23 (width 1, unsigned)
 *   - mbstate_t availability through the header, and zero-width reset shape
 *   - include-guard idempotence
 *
 * WHAT IS NOT TESTED (BLOCKED -- see libc/src/uchar/uchar.c)
 *   The six conversion entry points have no backend: the host exports none of
 *   mbrtoc16/c16rtomb/mbrtoc32/c32rtomb/mbrtoc8/c8rtomb (link probe fails with
 *   all six undefined), and no vendored musl implementation exists locally.
 *   Consequently the originally requested conversion cases cannot run:
 *     * valid ASCII 'A' -> 0x0041 / 0x00000041 with a zeroed mbstate_t
 *     * length-only / zero-width reset queries (NULL destination, NULL state)
 *     * invalid-byte classification ((size_t)-1 with errno == EILSEQ)
 *     * cplus_libc_log() entry logging parity on each wrapper
 *   Including those calls here would fail to link and would require inventing
 *   an implementation, which the integration rules forbid. They are therefore
 *   documented, not stubbed.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cplus_uchar.h"
#include "cplus_uchar.h" /* include guard must make this a no-op */

int main(void) {
    mbstate_t st;

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

    /* --- copied musl UTF-16/UTF-32 conversions --------------------------- */
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

    puts("uchar: ok");
    return 0;
}
