/* Standalone C99 test for the vendored safeclib memchr_s / memccpy_s
 * implementations brought in by libc/src/safecrt_vendor/mem_group.c.
 *
 * Link: /tmp/mem_group.o + build/safe/libcplus_safecrt.a +
 *       build/libc/libcplus_hardened.a
 *
 * Prints: vendor_mem: <checks> <failures>
 * Exits nonzero when any check fails.
 */
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "cplus_vendor_mem.h"

/* Pinned safeclib error codes (third_party/safeclib/include/safe_lib_errno.h
 * @ 90b076e94c4b0). Defined with guards so the test stays self-contained. */
#ifndef EOK
#define EOK 0
#endif
#ifndef ESNULLP
#define ESNULLP 400
#endif
#ifndef ESZEROL
#define ESZEROL 401
#endif
#ifndef ESLEMAX
#define ESLEMAX 403
#endif
#ifndef ESNOSPC
#define ESNOSPC 406
#endif
#ifndef ESNOTFND
#define ESNOTFND 409
#endif

/* The BOS-aware entry points exported by mem_group.c. Declared locally with
 * plain signatures (attributes are irrelevant to linkage). They are used
 * below to exercise the one-over-dmax constraint with an explicit BOS, which
 * the public BOS_UNKNOWN shims cannot detect. */
extern errno_t cplus_v__memchr_s_chk(const void *dest, rsize_t dmax, int ch,
                                     void **resultp, size_t destbos);
extern errno_t cplus_v__memccpy_s_chk(void *dest, rsize_t dmax,
                                      const void *src, int c, rsize_t n,
                                      size_t destbos, size_t srcbos);

static int checks = 0;
static int failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        ++checks;                                                              \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            printf("FAIL: %s (rc=%d)\n", (msg), (int)rc);                      \
        }                                                                      \
    } while (0)

int main(void) {
    errno_t rc;

    /* ---- memchr_s: valid input matches ordinary memchr ---- */
    {
        const char hay[] = "hello world";
        void *res = NULL;
        void *expect = memchr(hay, 'o', sizeof(hay) - 1);

        rc = cplus_v_memchr_s(hay, sizeof(hay) - 1, 'o', &res);
        CHECK(rc == EOK, "memchr_s valid rc");
        CHECK(res == expect, "memchr_s valid pointer matches memchr");
        CHECK(res != NULL && *(const char *)res == 'o', "memchr_s valid byte");
    }

    /* ---- memchr_s: not found ---- */
    {
        const char hay[] = "hello world";
        void *res = (void *)hay;

        rc = cplus_v_memchr_s(hay, sizeof(hay) - 1, 'z', &res);
        CHECK(rc == ESNOTFND, "memchr_s not-found rc");
        CHECK(res == NULL, "memchr_s not-found result is NULL");
    }

    /* ---- memchr_s: NULL dest ---- */
    {
        void *res = (void *)&checks;

        rc = cplus_v_memchr_s(NULL, 4, 'a', &res);
        CHECK(rc == ESNULLP, "memchr_s NULL dest rc");
        CHECK(res == NULL, "memchr_s NULL dest result cleared");
    }

    /* ---- memchr_s: NULL resultp ---- */
    {
        const char hay[] = "abc";

        rc = cplus_v_memchr_s(hay, sizeof(hay) - 1, 'a', NULL);
        CHECK(rc == ESNULLP, "memchr_s NULL resultp rc");
    }

    /* ---- memchr_s: zero dmax ---- */
    {
        const char hay[] = "abc";
        void *res = (void *)hay;

        rc = cplus_v_memchr_s(hay, 0, 'a', &res);
        CHECK(rc == ESZEROL, "memchr_s zero dmax rc");
        CHECK(res == NULL, "memchr_s zero dmax result cleared");
    }

    /* ---- memchr_s: one-over dmax (explicit BOS via _chk) ---- */
    {
        char small[4] = {'a', 'b', 'c', 'd'};
        void *res = (void *)small;

        rc = cplus_v__memchr_s_chk(small, sizeof(small) + 1, 'b', &res,
                                   (size_t)sizeof(small));
        CHECK(rc == EOVERFLOW, "memchr_s one-over dmax rc");
    }

    /* ---- memccpy_s: valid input matches ordinary memccpy ---- */
    {
        const char src[] = "abcXdef";
        char dst[16];
        char ref[16];
        void *pr;

        memset(dst, 0, sizeof(dst));
        memset(ref, 0, sizeof(ref));
        pr = memccpy(ref, src, 'X', sizeof(src) - 1);

        rc = cplus_v_memccpy_s(dst, sizeof(dst), src, 'X', sizeof(src) - 1);
        CHECK(rc == EOK, "memccpy_s valid rc");
        CHECK(pr != NULL, "memccpy reference found delimiter");
        CHECK(memcmp(dst, ref, sizeof(ref)) == 0,
              "memccpy_s valid bytes match memccpy");
    }

    /* ---- memccpy_s: n == 0 is EOK and writes NUL ---- */
    {
        char dst[8];
        char src[] = "xyz";

        memset(dst, 'A', sizeof(dst));
        rc = cplus_v_memccpy_s(dst, sizeof(dst), src, 'x', 0);
        CHECK(rc == EOK, "memccpy_s n==0 rc");
        CHECK(dst[0] == '\0', "memccpy_s n==0 writes NUL");
    }

    /* ---- memccpy_s: NULL dest ---- */
    {
        char src[] = "abc";

        rc = cplus_v_memccpy_s(NULL, 8, src, 'a', 3);
        CHECK(rc == ESNULLP, "memccpy_s NULL dest rc");
    }

    /* ---- memccpy_s: NULL src ---- */
    {
        char dst[8];

        memset(dst, 'A', sizeof(dst));
        rc = cplus_v_memccpy_s(dst, sizeof(dst), NULL, 'a', 3);
        CHECK(rc == ESNULLP, "memccpy_s NULL src rc");
        /* C+ SECURITY PATCH: on a constraint refusal the destination is NOT
         * cleared (upstream's clear used the caller's dmax and could overrun).
         * The refusal code is the contract; dst must remain intact. */
        CHECK(dst[0] != '\0', "memccpy_s NULL src leaves dest intact");
    }

    /* ---- memccpy_s: zero dmax ---- */
    {
        char dst[8];
        char src[] = "abc";

        rc = cplus_v_memccpy_s(dst, 0, src, 'a', 3);
        CHECK(rc == ESZEROL, "memccpy_s zero dmax rc");
    }

    /* ---- memccpy_s: n > dmax ---- */
    {
        char dst[8];
        char src[] = "abcdefghij";

        rc = cplus_v_memccpy_s(dst, 4, src, 'z', 10);
        CHECK(rc == ESNOSPC, "memccpy_s n>dmax rc");
    }

    /* ---- memccpy_s: not found with insufficient space ---- */
    {
        char dst[8];
        char src[] = "abcdef";

        rc = cplus_v_memccpy_s(dst, 5, src, 'Z', 6);
        CHECK(rc == ESNOSPC, "memccpy_s not-found rc");
    }

    /* ---- memccpy_s: one-over dmax (explicit BOS via _chk) ---- */
    {
        char dst[8];
        char src[] = "abc";

        rc = cplus_v__memccpy_s_chk(dst, sizeof(dst) + 1, src, 'a', 3,
                                    (size_t)sizeof(dst), (size_t)sizeof(src));
        CHECK(rc == EOVERFLOW, "memccpy_s one-over dmax rc");
    }

    printf("vendor_mem: %d %d\n", checks, failures);
    return failures != 0;
}
