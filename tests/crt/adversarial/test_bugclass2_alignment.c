/*
 * Bug CLASS: misaligned / non-word-size copy.
 *
 * Raw C folklore says memcpy needs word-aligned pointers and word-sized
 * lengths. It does not: memcpy/memmove are byte-granular. The checked C+
 * cplus_memcpy_s is likewise byte-granular AND bounded: it copies exactly
 * `smax` bytes into a region the CALLER bounded by `dmax`, and it refuses a
 * request whose `smax` exceeds `dmax`. So odd sizes and misaligned pointers
 * copy correctly, and an over-long request is rejected with no byte written
 * past the caller's truthful `dmax`.
 *
 * The bound passed here is always the real size of the destination object, so
 * the rejections below are genuine constraint catches, not self-inflicted ones.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h" /* ESNOSPC, ESLEMAX */

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* --- misaligned destination, odd length: byte copy succeeds in-bounds --- */
    {
        unsigned char arena[32];
        const unsigned char src[8] = { 0x11, 0x22, 0x33, 0x44,
                                       0x55, 0x66, 0x77, 0x88 };
        unsigned char *dst = arena + 1;        /* deliberately misaligned */
        memset(arena, 0xEE, sizeof arena);
        CHECK(cplus_memcpy_s(dst, 5, src, 5) == 0);
        CHECK(memcmp(dst, src, 5) == 0);
        CHECK(arena[0] == 0xEE);               /* byte before bound untouched  */
        CHECK(arena[6] == 0xEE);               /* byte after bound untouched   */
    }

    /* --- misaligned source, odd length: byte copy succeeds --- */
    {
        unsigned char srcbuf[16];
        unsigned char dst[8];
        int i;
        for (i = 0; i < 16; ++i) srcbuf[i] = (unsigned char)(0x40 + i);
        memset(dst, 0x00, sizeof dst);
        CHECK(cplus_memcpy_s(dst, 3, srcbuf + 3, 3) == 0);
        CHECK(dst[0] == 0x43 && dst[1] == 0x44 && dst[2] == 0x45);
        CHECK(dst[3] == 0x00);                 /* only the 3 requested bytes   */
    }

    /* --- byte-granular loop over odd sizes, both ends misaligned --- */
    {
        const size_t odd[] = { 1, 3, 5, 7 };
        size_t k;
        for (k = 0; k < sizeof odd / sizeof odd[0]; ++k) {
            unsigned char s[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
            unsigned char d[8];
            size_t n = odd[k];
            memset(d, 0, sizeof d);
            CHECK(cplus_memcpy_s(d + 1, n, s + 1, n) == 0);
            CHECK(memcmp(d + 1, s + 1, n) == 0);
        }
    }

    /* --- bounded: a 4-byte write must not touch the 5th guard byte --- */
    {
        unsigned char buf[8];
        memset(buf, 0x7A, sizeof buf);
        CHECK(cplus_memcpy_s(buf, 4, "WXYZ", 4) == 0);
        CHECK(memcmp(buf, "WXYZ", 4) == 0);
        CHECK(buf[4] == 0x7A && buf[5] == 0x7A);
    }

    /* --- oversized smax (> truthful dmax) rejected; guard bytes intact --- */
    {
        unsigned char buf[8];
        errno_t r;
        memset(buf, 0x5C, sizeof buf);
        r = cplus_memcpy_s(buf, 4, "ABCDEFGH", 8);
        CHECK(r != 0);
        CHECK(r == ESNOSPC);
        CHECK(buf[4] == 0x5C && buf[7] == 0x5C);
    }

    /* --- oversized dmax (> RSIZE_MAX_MEM) rejected --- */
    {
        unsigned char d[8];
        errno_t r;
        memset(d, 0x11, sizeof d);
        r = cplus_memcpy_s(d, RSIZE_MAX_MEM + 1, "x", 1);
        CHECK(r != 0);
        CHECK(r == ESLEMAX);
    }

    /* --- oversized smax (> RSIZE_MAX_MEM) rejected --- */
    {
        unsigned char d[8];
        errno_t r = cplus_memcpy_s(d, sizeof d, "x", RSIZE_MAX_MEM + 1);
        CHECK(r != 0);
        CHECK(r == ESLEMAX);
    }

    /* --- misaligned memmove_s is bounded too, and shifts correctly --- */
    {
        char mv[16] = "0123456789";
        CHECK(cplus_memmove_s(mv + 1, 5, mv + 3, 5) == 0);
        CHECK(memcmp(mv + 1, "34567", 5) == 0);
    }

    if (failures == 0) {
        printf("bugclass2_alignment: ok\n");
        return 0;
    }
    printf("bugclass2_alignment: FAILURES=%d\n", failures);
    return 1;
}
