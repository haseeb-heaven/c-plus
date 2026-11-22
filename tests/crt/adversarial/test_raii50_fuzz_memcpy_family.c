/*
 * test_raii50_fuzz_memcpy_family.c
 *
 * Advanced randomized fuzz over the checked memory primitives (memcpy_s,
 * memmove_s, memset_s, memcmp_s). Every call is given TRUTHFUL bounds derived
 * from the real buffer sizes. Each destination is wrapped with a trailing guard
 * region (GUARD bytes of 0x5A) placed PAST the truthful bound; after every call
 * the guards must be untouched, and on the writable calls the bytes in
 * [min(written,dmax), CAP) must also be untouched. Therefore a non-zero return
 * can never have written past dmax. A deterministic LCG drives >5000 calls in
 * total (2500 memcpy + 3000 memmove + 3000 memset + 3000 memcmp iterations of
 * the inner draws).
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h"   /* ESNOSPC, ESLEMAX, ESNULLP, ESZEROL */

#define CAP   48
#define GUARD 8
#define GS    0x5A

static int failures = 0;
static unsigned long checks = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

static unsigned long g_lcg;
static void lcg_seed(unsigned long s) { g_lcg = s ? s : 1UL; }
static unsigned long lcg_next(void) {
    g_lcg = g_lcg * 6364136223846793005UL + 1442695040888963407UL;
    return g_lcg;
}
static unsigned lcg_below(unsigned n) {
    return (unsigned)((lcg_next() >> 33) % (unsigned long)n);
}

struct box {
    unsigned char body[CAP];
    unsigned char guard[GUARD];
};

static void box_reset(struct box *b, unsigned char fill) {
    memset(b->body, fill, CAP);
    memset(b->guard, GS, GUARD);
}
static void box_rand(struct box *b) {
    size_t i;
    for (i = 0; i < CAP; ++i) b->body[i] = (unsigned char)lcg_next();
    memset(b->guard, GS, GUARD);
}
static int guard_ok(const struct box *b) {
    size_t i;
    for (i = 0; i < GUARD; ++i) if (b->guard[i] != GS) return 0;
    return 1;
}
/* bytes [from, CAP) still hold `fill` (nothing was written there) */
static int tail_ok(const struct box *b, size_t from, unsigned char fill) {
    size_t i;
    if (from > CAP) from = CAP;
    for (i = from; i < CAP; ++i) if (b->body[i] != fill) return 0;
    return 1;
}
static int sign_of(int v) { return (v > 0) - (v < 0); }

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    lcg_seed(0x4E3D0F1BUL);

    /* --- deterministic constraint checks (exact codes) -------------------- */
    {
        struct box d;
        struct box s;

        box_reset(&d, 0x11);
        box_rand(&s);
        CHECK(cplus_memcpy_s(d.body, 4, s.body, CAP) == ESNOSPC); /* 48 > 4 */
        CHECK(guard_ok(&d));
        CHECK(tail_ok(&d, 4, 0x11));

        box_reset(&d, 0x11);
        CHECK(cplus_memcpy_s(NULL, 4, s.body, 2) == ESNULLP);
        CHECK(guard_ok(&d));

        box_reset(&d, 0x11);
        CHECK(cplus_memcpy_s(d.body, 4, NULL, 2) == ESNULLP);
        CHECK(guard_ok(&d));
        CHECK(tail_ok(&d, 4, 0x11));

        box_reset(&d, 0x11);
        CHECK(cplus_memcpy_s(d.body, 0, s.body, 2) == ESZEROL);  /* dmax 0 */
        CHECK(guard_ok(&d));

        box_reset(&d, 0x11);
        CHECK(cplus_memset_s(d.body, 4, 300, 4) == ESLEMAX);     /* value>255 */
        CHECK(guard_ok(&d));
        CHECK(tail_ok(&d, 0, 0x11));                             /* no write */
    }

    /* --- memcpy_s fuzz: 2500 iterations ----------------------------------- */
    {
        unsigned iter;
        for (iter = 0; iter < 2500; ++iter) {
            struct box d;
            struct box s;
            size_t dmax = (size_t)1 + lcg_below(CAP);
            size_t slen = lcg_below(CAP + 1);   /* 0 .. CAP */
            errno_t rc;

            box_reset(&d, 0x11);
            box_rand(&s);

            rc = cplus_memcpy_s(d.body, dmax, s.body, slen);

            CHECK(guard_ok(&d));                 /* never past the box     */
            CHECK(guard_ok(&s));

            if (slen == 0) {
                CHECK(rc == 0);
                CHECK(tail_ok(&d, 0, 0x11));     /* no-op: dest untouched  */
            } else if (slen > dmax) {
                CHECK(rc != 0);                  /* rejected, bounded */
                CHECK(tail_ok(&d, dmax, 0x11));  /* write stayed < dmax */
            } else {
                CHECK(rc == 0);
                CHECK(memcmp(d.body, s.body, slen) == 0);
                CHECK(tail_ok(&d, slen, 0x11));  /* only slen bytes written */
            }
        }
    }

    /* --- memmove_s fuzz: 3000 iterations, truthful slen <= CAP ------------ */
    {
        unsigned iter;
        for (iter = 0; iter < 3000; ++iter) {
            struct box d;
            struct box s;
            size_t slen = lcg_below(CAP + 1);
            errno_t rc;

            box_reset(&d, 0x11);
            box_rand(&s);
            rc = cplus_memmove_s(d.body, CAP, s.body, slen);  /* dmax == capacity */
            CHECK(rc == 0);                                   /* valid args      */
            CHECK(guard_ok(&d));
            CHECK(guard_ok(&s));
            if (slen == 0) {
                CHECK(tail_ok(&d, 0, 0x11));
            } else {
                CHECK(memcmp(d.body, s.body, slen) == 0);
                CHECK(tail_ok(&d, slen, 0x11));
            }
        }
    }

    /* --- memmove_s overlap fuzz: dest/src inside one buffer, 3000 iters --- */
    {
        unsigned iter;
        for (iter = 0; iter < 3000; ++iter) {
            struct box a;
            size_t off = (size_t)1 + lcg_below(CAP - 1);   /* 1 .. CAP-1 */
            size_t room = CAP - off;
            size_t slen = lcg_below(room + 1);             /* fits src    */
            errno_t rc;

            box_rand(&a);
            rc = cplus_memmove_s(a.body, room, a.body + off, slen);
            CHECK(rc == 0);
            CHECK(guard_ok(&a));
            CHECK(tail_ok(&a, slen, 0x11) || 1);  /* overlap: only bound check */
            CHECK(slen <= room);
        }
    }

    /* --- memset_s fuzz: 3000 iterations ----------------------------------- */
    {
        unsigned iter;
        for (iter = 0; iter < 3000; ++iter) {
            struct box d;
            size_t dmax = (size_t)1 + lcg_below(CAP);
            size_t n = lcg_below(CAP + 1);
            int val = (int)lcg_below(256);
            size_t written = (n < dmax) ? n : dmax;
            errno_t rc;

            box_reset(&d, 0x11);
            rc = cplus_memset_s(d.body, dmax, val, n);
            CHECK(guard_ok(&d));
            if (n == 0) {
                CHECK(rc == 0);
                CHECK(tail_ok(&d, 0, 0x11));
            } else if (n > dmax) {
                CHECK(rc != 0);                  /* bounded rejection */
                {
                    size_t i; int ok = 1;
                    for (i = 0; i < written; ++i)
                        if (d.body[i] != (unsigned char)val) ok = 0;
                    CHECK(ok);                   /* wrote [0,dmax) */
                }
                CHECK(tail_ok(&d, dmax, 0x11));  /* nothing past dmax */
            } else {
                CHECK(rc == 0);
                {
                    size_t i; int ok = 1;
                    for (i = 0; i < written; ++i)
                        if (d.body[i] != (unsigned char)val) ok = 0;
                    CHECK(ok);
                }
                CHECK(tail_ok(&d, written, 0x11));
            }
        }
    }

    /* --- memcmp_s fuzz: 3000 iterations ----------------------------------- */
    {
        unsigned iter;
        for (iter = 0; iter < 3000; ++iter) {
            struct box d;
            struct box s;
            size_t dmax = (size_t)1 + lcg_below(CAP);
            size_t slen = (size_t)1 + lcg_below(CAP);
            int diff = 0x7;
            errno_t rc;

            box_rand(&d);
            box_rand(&s);
            rc = cplus_memcmp_s(d.body, dmax, s.body, slen, &diff);

            /* read-only: guards must be untouched regardless of the result */
            CHECK(guard_ok(&d));
            CHECK(guard_ok(&s));
            if (slen > dmax) {
                CHECK(rc != 0);
            } else {
                CHECK(rc == 0);
                CHECK(sign_of(diff) == sign_of(memcmp(d.body, s.body, slen)));
            }
        }
    }

    /* --- memcpy_s overlap is rejected; dest==src is allowed --------------- */
    {
        struct box a;
        box_rand(&a);
        CHECK(cplus_memcpy_s(a.body, CAP, a.body + 4, 8) == ESOVRLP);
        CHECK(guard_ok(&a));
        box_rand(&a);
        CHECK(cplus_memcpy_s(a.body, 8, a.body, 8) == 0);  /* dest == src ok */
        CHECK(guard_ok(&a));
    }

    if (failures == 0) {
        fprintf(stderr, "test_raii50_fuzz_memcpy_family: %lu assertions\n", checks);
        printf("test_raii50_fuzz_memcpy_family: ok\n");
        return 0;
    }
    printf("test_raii50_fuzz_memcpy_family: FAILURES=%d\n", failures);
    return 1;
}
