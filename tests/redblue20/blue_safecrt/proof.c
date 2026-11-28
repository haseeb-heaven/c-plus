/*
 * BLUE-20 proof: the checked SafeCRT layer refuses malformed inputs while
 * leaving adjacent canary bytes intact.
 *
 * Eight checked adapters are exercised through their public macro spellings
 * (declared in stdlib/safe/safe_memory.h and stdlib/safe/safe_string.h):
 *   memcpy_s memmove_s memset_s memcmp_s strcpy_s strncpy_s strcat_s strncat_s
 *
 * Six violation classes are exercised for every adapter:
 *   null, zero, one-over, SIZE_MAX, overlap, unterminated-source
 *
 * Every case runs in its own forked child (bounds a crash to one case), under
 * a fixed-seed deterministic PRNG.  A refusal that writes past the declared
 * capacity corrupts the canary and fails the case.  Where an API is designed
 * to HANDLE a condition rather than refuse it (memmove_s overlap, memcmp_s
 * overlap, and the single-pointer/binary APIs where a class is not a
 * constraint violation), the case instead requires success + intact canary
 * (+ result correctness); this is documented in PROOF.md.
 *
 * Build/link (see run.sh):
 *   clang -std=c99 -Istdlib/safe -Ilibc/install-include \
 *         -Ithird_party/safeclib/include proof.c \
 *         build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *         -lm -pthread -o out/proof
 */
#define _POSIX_C_SOURCE 200112L
#include "safe_memory.h"
#include "safe_string.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- support symbols (present in libcplus_safecrt.a) -------------------- */
extern void cplus_runtime_set_violation_trap(int enabled);
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(
    void (*h)(const char *, void *, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(
    void (*h)(const char *, void *, int));

/* ---- layout: capacity followed by an explicit canary region ------------- */
#define CANBYTES     32u
#define CAPMIN       16
#define CAPMAX       31
#define DBUFSZ       (CAPMAX + CANBYTES)   /* 63 */
#define SBUFSZ       128
#define S_DATA_EXTRA 8                     /* source data = cap + 8 bytes */
#define CANVAL       0xC3u
#define DVAL         0x5Au

static unsigned char D[DBUFSZ];  /* destination [0,cap) data, [cap,..) canary */
static unsigned char S[SBUFSZ];  /* source      [0,slen) data, [slen,..) canary */

static void noop_handler(const char *msg, void *ptr, int err) {
    (void)msg; (void)ptr; (void)err;
}

/* xorshift32: fixed-seed, per-case deterministic, no platform PRNG. */
static uint32_t rstate;
static uint32_t rnd(void) {
    rstate ^= rstate << 13;
    rstate ^= rstate >> 17;
    rstate ^= rstate << 5;
    return rstate;
}

static int canaries_ok(int cap) {
    int i;
    int slen = cap + S_DATA_EXTRA;
    for (i = cap; i < cap + (int)CANBYTES; ++i)
        if (D[i] != CANVAL) return 0;
    for (i = slen; i < SBUFSZ; ++i)
        if (S[i] != CANVAL) return 0;
    return 1;
}

/* Initialize buffers. src_term: place a NUL at S[slen-1]; otherwise the
 * source data region is entirely non-NUL (an unterminated source). */
static void setup(int cap, unsigned char fill, int src_term) {
    int i;
    int slen = cap + S_DATA_EXTRA;
    for (i = 0; i < DBUFSZ; ++i) D[i] = (i < cap) ? DVAL : (unsigned char)CANVAL;
    for (i = 0; i < SBUFSZ; ++i) S[i] = (unsigned char)CANVAL;
    for (i = 0; i < slen; ++i) S[i] = (unsigned char)(fill + (unsigned char)(i % 7));
    if (src_term) S[slen - 1] = 0;
}

/* Run one case.  Returns 0 on PASS, nonzero on FAIL. */
static int run_case(int idx) {
    int api = idx % 8;
    int cat = (idx / 8) % 6;
    int cap, off, slen, rc, expect_refusal, check_correct, src_term, ok;
    unsigned char fill, orig[DBUFSZ];
    int diff = 1234567;
    size_t i;

    rstate = 0x5EED1234u ^ (uint32_t)idx * 0x9E3779B9u;
    cap = CAPMIN + (int)(rnd() % (uint32_t)(CAPMAX - CAPMIN + 1));
    fill = (unsigned char)('A' + (rnd() % 20u));
    off = 1 + (int)(rnd() % 4u);
    slen = cap + S_DATA_EXTRA;

    expect_refusal = 1;     /* most classes must be refused */
    check_correct = 0;
    /* String APIs need a terminated source except in the unterminated class. */
    src_term = (api >= 4) && (cat != 5);

    setup(cap, fill, src_term);
    memcpy(orig, D, sizeof D);
    errno = 0;

    switch (api) {
    /* ---------------- 0: memcpy_s ------------------------------------- */
    case 0:
        switch (cat) {
        case 0: rc = memcpy_s(D, cap, NULL, 4); break;                       /* null src */
        case 1: rc = memcpy_s(D, 0, S, 4); break;                            /* zero dmax */
        case 2: rc = memcpy_s(D, cap, S, (rsize_t)cap + 1); break;           /* one-over smax */
        case 3: rc = memcpy_s(D, cap, S, (rsize_t)-1); break;                /* SIZE_MAX */
        case 4: rc = memcpy_s(D, cap, D + off, (size_t)(cap / 2)); break;    /* overlap */
        default: rc = memcpy_s(D, cap, S, (size_t)cap); expect_refusal = 0;  /* binary (no NUL) */
        }
        break;
    /* ---------------- 1: memmove_s ------------------------------------ */
    case 1:
        switch (cat) {
        case 0: rc = memmove_s(D, cap, NULL, 4); break;
        case 1: rc = memmove_s(D, 0, S, 4); break;
        case 2: rc = memmove_s(D, cap, S, (rsize_t)cap + 1); break;
        case 3: rc = memmove_s(D, cap, S, (rsize_t)-1); break;
        case 4: /* overlap is handled, not refused: verify defined result */
            rc = memmove_s(D, cap, D + off, (size_t)(cap - off));
            expect_refusal = 0; check_correct = 1; break;
        default: rc = memmove_s(D, cap, S, (size_t)cap); expect_refusal = 0;
        }
        break;
    /* ---------------- 2: memset_s ------------------------------------- */
    case 2:
        switch (cat) {
        case 0: rc = memset_s(NULL, cap, fill, 4); break;                    /* null dest */
        case 1: rc = memset_s(D, 0, fill, 4); break;                         /* zero dmax */
        case 2: rc = memset_s(D, cap, fill, (rsize_t)cap + 1); break;        /* one-over n */
        case 3: rc = memset_s(D, cap, fill, (rsize_t)-1); break;             /* SIZE_MAX */
        case 4: /* single pointer: no overlap possible; fill up to boundary */
            rc = memset_s(D, cap, fill, (rsize_t)cap);
            expect_refusal = 0; break;
        default: /* non-NUL binary fill */
            rc = memset_s(D, cap, 0x41, (rsize_t)cap); expect_refusal = 0;
        }
        break;
    /* ---------------- 3: memcmp_s ------------------------------------- */
    case 3:
        switch (cat) {
        case 0: rc = memcmp_s(D, cap, NULL, 4, &diff); break;
        case 1: rc = memcmp_s(D, 0, S, 4, &diff); break;
        case 2: rc = memcmp_s(D, cap, S, (rsize_t)cap + 1, &diff); break;
        case 3: rc = memcmp_s(D, cap, S, (rsize_t)-1, &diff); break;
        case 4: /* full self-overlap: compare D against itself => equal */
            rc = memcmp_s(D, cap, D, (rsize_t)cap, &diff);
            expect_refusal = 0; check_correct = 1; break;
        default: rc = memcmp_s(D, cap, S, (size_t)cap, &diff); expect_refusal = 0;
        }
        break;
    /* ---------------- 4: strcpy_s ------------------------------------- */
    case 4:
        switch (cat) {
        case 0: rc = strcpy_s((char *)D, cap, NULL); break;                  /* null src */
        case 1: rc = strcpy_s((char *)D, 0, (char *)S); break;               /* zero dmax */
        case 2: rc = strcpy_s((char *)D, cap, (char *)S); break;             /* long string > cap */
        case 3: rc = strcpy_s((char *)D, cap, (char *)S); break;             /* no size arg: long str */
        case 4: /* src NUL-terminated inside D, overlapping dest */
            D[0] = 'a'; D[1] = 'b'; D[2] = 'c'; D[3] = 0;
            rc = strcpy_s((char *)D, cap, (char *)D + 1); break;
        default: rc = strcpy_s((char *)D, cap, (char *)S); break;            /* unterminated src */
        }
        break;
    /* ---------------- 5: strncpy_s ------------------------------------ */
    case 5:
        switch (cat) {
        case 0: rc = strncpy_s((char *)D, cap, NULL, 4); break;
        case 1: rc = strncpy_s((char *)D, 0, (char *)S, 4); break;
        case 2: rc = strncpy_s((char *)D, cap, (char *)S, (rsize_t)cap + 1); break;
        case 3: rc = strncpy_s((char *)D, cap, (char *)S, (rsize_t)-1); break;
        case 4:
            D[0] = 'a'; D[1] = 'b'; D[2] = 'c'; D[3] = 0;
            rc = strncpy_s((char *)D, cap, (char *)D + 1, 3); break;
        default: rc = strncpy_s((char *)D, cap, (char *)S, (rsize_t)cap); break;
        }
        break;
    /* ---------------- 6: strcat_s ------------------------------------- */
    case 6:
        D[0] = 'x'; D[1] = 0;
        switch (cat) {
        case 0: rc = strcat_s((char *)D, cap, NULL); break;
        case 1: rc = strcat_s((char *)D, 0, (char *)S); break;
        case 2: rc = strcat_s((char *)D, cap, (char *)S); break;
        case 3: rc = strcat_s((char *)D, cap, (char *)S); break;
        case 4:
            D[0] = 'x'; D[1] = 0; D[2] = 'a'; D[3] = 'b'; D[4] = 'c'; D[5] = 0;
            rc = strcat_s((char *)D, cap, (char *)D + 2); break;
        default: rc = strcat_s((char *)D, cap, (char *)S); break;
        }
        break;
    /* ---------------- 7: strncat_s ------------------------------------ */
    default: /* api == 7 */
        D[0] = 'x'; D[1] = 0;
        switch (cat) {
        case 0: rc = strncat_s((char *)D, cap, NULL, 4); break;
        case 1: rc = strncat_s((char *)D, 0, (char *)S, 4); break;
        case 2: rc = strncat_s((char *)D, cap, (char *)S, (rsize_t)cap + 1); break;
        case 3: rc = strncat_s((char *)D, cap, (char *)S, (rsize_t)-1); break;
        case 4:
            D[0] = 'x'; D[1] = 0; D[2] = 'a'; D[3] = 'b'; D[4] = 'c'; D[5] = 0;
            rc = strncat_s((char *)D, cap, (char *)D + 2, 3); break;
        default: rc = strncat_s((char *)D, cap, (char *)S, (rsize_t)cap); break;
        }
        break;
    }

    ok = 1;
    if (expect_refusal) {
        if (rc == 0) ok = 0;              /* a violation must be refused */
    } else {
        if (rc != 0) ok = 0;              /* safe-handled case must succeed */
    }
    if (!canaries_ok(cap)) ok = 0;        /* adjacent memory must be intact */

    if (check_correct && ok) {
        if (api == 1) {                   /* memmove overlap: shifted content */
            for (i = 0; i < (size_t)(cap - off); ++i)
                if (D[i] != orig[i + off]) { ok = 0; break; }
        } else if (api == 3) {            /* memcmp self-overlap: equal */
            if (diff != 0) ok = 0;
        }
    }

    if (!ok) {
        fprintf(stderr,
                "FAIL idx=%d api=%d cat=%d cap=%d off=%d rc=%d errno=%d "
                "expect_refusal=%d canary=%d diff=%d\n",
                idx, api, cat, cap, off, rc, errno, expect_refusal,
                canaries_ok(cap), diff);
    }
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    int total = (argc > 1) ? atoi(argv[1]) : 2400;
    int i, passed = 0, failed = 0;

    if (total <= 0) total = 2400;

    /* Quiet the safeclib constraint banners and the violation trap; the
     * checked layer still returns its constraint codes and honours canaries. */
    cplus_runtime_set_violation_trap(0);
    cplus_sc_set_mem_constraint_handler_s_wrapper(noop_handler);
    cplus_sc_set_str_constraint_handler_s_wrapper(noop_handler);

    fprintf(stderr, "BLUE20 safecrt proof: fixed_seed=0x5EED1234 cases=%d\n", total);

    for (i = 0; i < total; ++i) {
        pid_t p = fork();
        if (p == (pid_t)-1) { failed++; continue; }
        if (p == 0) {
            alarm(5);                       /* bound a hung child */
            _exit(run_case(i));
        } else {
            int st = 0;
            if (waitpid(p, &st, 0) == (pid_t)-1) { failed++; continue; }
            if (WIFEXITED(st) && WEXITSTATUS(st) == 0) passed++;
            else failed++;
        }
    }

    printf("BLUE20 safecrt %d %d %d\n", total, passed, failed);
    return failed ? 1 : 0;
}
