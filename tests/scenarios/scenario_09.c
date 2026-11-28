/*
 * scenario_09.c -- TEST 9: "partial initialization -> use uninitialized fields"
 * ============================================================================
 *
 * This file has TWO independent parts.
 *
 * (A) RUNTIME PART (default build -- the only part the runner executes)
 * ---------------------------------------------------------------------
 * 1280 DISTINCT deterministic (struct_size, init_pattern, read_field, seed)
 * tuples are executed with NO REAL UNDEFINED BEHAVIOUR.  Every field that is
 * read has a defined value, obtained in one of five ways:
 *
 *   pattern 0: calloc_s()                       -> whole struct zeroed
 *   pattern 1: malloc_s() + memset of FULL struct -> whole struct zeroed
 *   pattern 2: malloc_s() + explicit init of the first k fields; the read
 *              index is chosen from [0,k), i.e. only initialized fields
 *   pattern 3: ordinary malloc() spelled through safe_c.h (-> malloc_s),
 *              followed by memset of the FULL struct before the read
 *   pattern 4: malloc_s() + explicit init of EVERY field
 *
 * The hardened layer is verified on every case (tracked size / liveness) and
 * once explicitly: an oversized memset is refused and a trailing canary
 * survives.  Final line of a successful run:
 *     scenario_09: 1280 runtime cases ok
 *
 * (B) COMPILE-TIME PART -- NEGATIVE FIXTURE (measurement, never executed)
 * -----------------------------------------------------------------------
 * 500 DISTINCT functions `uninit_1` .. `uninit_500`, each carrying a
 * "CASE n" marker comment, read a field of a partially initialized
 * malloc'd struct:
 * fields [0,k) are written, field k (or later) is never written and is then
 * read.  A sound checker SHOULD emit diagnostics on many/all of them.
 *
 * The fixture is gated by
 *     #if defined(CPLUS_UNINIT_COMPILE_PROBE) || defined(CPLUS)
 *
 * The `-DCPLUS_UNINIT_COMPILE_PROBE` spelling activates it for the clang
 * backend that `c+ check` drives.  `CPLUS` is defined internally by the c+
 * compiler itself, so its SOURCE-LEVEL analyser also sees the block; that is
 * what makes `[CPLUS-...]` diagnostics observable.  A plain clang/C99 build
 * (tests/scenarios/run_scenarios.sh) defines neither macro and therefore sees
 * only the safe runtime part.  The runtime part uses the complementary guard,
 * so the probe build sees only the negative fixture (this also keeps the
 * SafeCRT headers, which c+ check does not add to its include path, out of the
 * probe translation unit).  These functions are NEVER built or run.
 *
 * MEASUREMENT COMMAND:
 *   ./build/c+ check -DCPLUS_UNINIT_COMPILE_PROBE \
 *       tests/scenarios/scenario_09.c --default
 *
 * HONEST EXPECTATION / FINDING
 * ----------------------------
 * The checker's `[CPLUS-LIFETIME]` rule tracks *local scalars*, not heap
 * object contents.  Each probe therefore also mirrors the uninitialized
 * field into a conditionally-initialized local `v`; when the checker reports
 * `v`, it has caught the PROPAGATED uninitialized value, NOT the raw heap
 * field read itself.  The accompanying report states both counts separately.
 */

/* The runtime part needs the SafeCRT headers (which c+ check's clang backend
 * does not add to its include path).  The compile-probe build therefore sees
 * ONLY the negative fixture below, and the runtime build sees ONLY this part. */
#if !defined(CPLUS_UNINIT_COMPILE_PROBE) && !defined(CPLUS)

#include "stdlib/safe/safe_c.h"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "safe_log.h"

#define RUNTIME_CASES  1280u
#define MAXF           16u

typedef struct {
    int f[MAXF];
} Fields;

static unsigned g_failures = 0u;

static void note_failure(unsigned case_id, const char *what) {
    if (g_failures < 12u)
        fprintf(stderr, "scenario_09: case %u: %s\n", case_id, what);
    ++g_failures;
}

/* ---- one deterministic runtime case ----------------------------------- */
static void run_case(unsigned id) {
    unsigned nf   = 1u + (id % MAXF);              /* 1..16 active fields   */
    unsigned pat  = (id / 16u) % 5u;               /* 0..4 init strategy    */
    unsigned slot = (id / 80u) % 4u;               /* 0..3 read slot        */
    unsigned seed = (id / 320u) % 4u;              /* 0..3 value seed       */
    size_t   bytes = (size_t)nf * sizeof(int);
    unsigned k     = 1u + (seed % nf);             /* explicit-init prefix  */
    unsigned read_idx;
    int      expected = 0;
    Fields  *p = NULL;
    unsigned j;

    slot %= nf;
    read_idx = slot;
    if (pat == 2u) read_idx %= k;                  /* never read uninit     */

    switch (pat) {
    case 0u:
        /* calloc_s() zero-fills the entire tracked block. */
        p = (Fields *)calloc_s(1u, bytes);
        expected = 0;
        break;
    case 1u:
        /* malloc_s + memset of the FULL struct before any read. */
        p = (Fields *)malloc_s(bytes);
        if (p != NULL) memset(p, 0, bytes);
        expected = 0;
        break;
    case 2u:
        /* Genuine partial init: only [0,k) are written; read stays in [0,k). */
        p = (Fields *)malloc_s(bytes);
        if (p != NULL)
            for (j = 0u; j < k; ++j)
                p->f[j] = (int)(seed * 1000u + j + 1u);
        expected = (int)(seed * 1000u + read_idx + 1u);
        break;
    case 3u:
        /* ORDINARY malloc via safe_c.h, then memset of the FULL struct. */
        p = (Fields *)malloc(bytes);
        if (p != NULL) memset(p, 0, bytes);
        expected = 0;
        break;
    default:
        /* malloc_s + explicit initialization of EVERY field. */
        p = (Fields *)malloc_s(bytes);
        if (p != NULL)
            for (j = 0u; j < nf; ++j)
                p->f[j] = (int)(20000u + seed * 100u + j);
        expected = (int)(20000u + seed * 100u + read_idx);
        break;
    }

    if (p == NULL) { note_failure(id, "allocation failed"); return; }
    if (!cplus_safe_ptr_is_live(p)) note_failure(id, "block not live");
    if (cplus_safe_ptr_size(p) != bytes) note_failure(id, "tracked size mismatch");

    /* No uninitialized value is consumed: read_idx is zeroed or explicitly set. */
    if (p->f[read_idx] != expected) note_failure(id, "unexpected field value");

    free(p);
    if (cplus_safe_ptr_is_live(p)) note_failure(id, "block still live after free");
}

/* ---- explicit hardened-layer capacity check ---------------------------- */
static void hardened_capacity_check(void) {
    unsigned char *p;
    const size_t n = 64u;
    errno_t rc;

    p = (unsigned char *)malloc_s(n + 8u);
    if (p == NULL) { note_failure(0u, "guard allocation failed"); return; }
    memset(p + n, 0xA5, 8u);                 /* trailing canary */
    rc = cplus_memset_s(p, n, 0x00, n + 1u); /* one byte too many */
    if (rc == 0) note_failure(0u, "oversized memset was not refused");
    if (p[n] != 0xA5) note_failure(0u, "canary corrupted by refused memset");
    free_s(p);
}

int main(void) {
    unsigned i;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    for (i = 0u; i < RUNTIME_CASES; ++i) run_case(i);
    hardened_capacity_check();

    if (g_failures != 0u) {
        printf("scenario_09: FAILURES=%u\n", g_failures);
        return 1;
    }
    printf("scenario_09: %u runtime cases ok\n", RUNTIME_CASES);
    return 0;
}

#endif /* !CPLUS_UNINIT_COMPILE_PROBE && !CPLUS -- runtime part */

/* ====================================================================== */
/* COMPILE-TIME NEGATIVE FIXTURE -- never built or executed by the runner.   */
/* ====================================================================== */
#if defined(CPLUS_UNINIT_COMPILE_PROBE) || defined(CPLUS)

#include <stdlib.h>

static int g_uninit_sink;

typedef struct { int f0; } U1;

typedef struct { int f0, f1; } U2;
typedef struct { int f0, f1, f2; } U3;
typedef struct { int f0, f1, f2, f3; } U4;
typedef struct { int f0, f1, f2, f3, f4; } U5;
typedef struct { int f0, f1, f2, f3, f4, f5; } U6;
typedef struct { int f0, f1, f2, f3, f4, f5, f6; } U7;
typedef struct { int f0, f1, f2, f3, f4, f5, f6, f7; } U8;
typedef struct { int f0, f1, f2, f3, f4, f5, f6, f7, f8; } U9;
typedef struct { int f0, f1, f2, f3, f4, f5, f6, f7, f8, f9; } U10;
typedef struct { int f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10; } U11;
typedef struct { int f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11; } U12;
typedef struct { int f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12; } U13;
typedef struct { int f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13; } U14;
typedef struct { int f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14; } U15;
typedef struct { int f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15; } U16;

/* CASE 1 */
int uninit_1(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 2 */
int uninit_2(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 3: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 3 */
int uninit_3(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 4 */
int uninit_4(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 5 */
int uninit_5(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 6 */
int uninit_6(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 2: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 7 */
int uninit_7(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 8 */
int uninit_8(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 9 */
int uninit_9(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 10 */
int uninit_10(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 1: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 11 */
int uninit_11(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 12 */
int uninit_12(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 13 */
int uninit_13(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 14 */
int uninit_14(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 5: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 15 */
int uninit_15(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 16 */
int uninit_16(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 17 */
int uninit_17(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 18 */
int uninit_18(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 307;
    int v;
    switch (p->f0) { case 4: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 19 */
int uninit_19(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 324;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 20 */
int uninit_20(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 341;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 21 */
int uninit_21(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 358;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 22 */
int uninit_22(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 375;
    int v;
    switch (p->f0) { case 3: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 23 */
int uninit_23(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 392;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 24 */
int uninit_24(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 409;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 25 */
int uninit_25(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 426;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 26 */
int uninit_26(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 443;
    int v;
    switch (p->f0) { case 2: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 27 */
int uninit_27(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 460;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 28 */
int uninit_28(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 477;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 29 */
int uninit_29(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 494;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 30 */
int uninit_30(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 511;
    int v;
    switch (p->f0) { case 1: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 31 */
int uninit_31(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 528;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 32 */
int uninit_32(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 545;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 33 */
int uninit_33(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 34 */
int uninit_34(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 5: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 35 */
int uninit_35(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 596;
    p->f1 = 599;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 36 */
int uninit_36(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 613;
    p->f1 = 616;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 37 */
int uninit_37(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 630;
    p->f1 = 633;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 38 */
int uninit_38(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 647;
    p->f1 = 650;
    int v;
    switch (p->f1) { case 4: v = p->f2; break; default: break; }
    free(p);
    return v;
}

/* CASE 39 */
int uninit_39(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 664;
    p->f1 = 667;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 40 */
int uninit_40(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 681;
    p->f1 = 684;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 41 */
int uninit_41(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 698;
    p->f1 = 701;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 42 */
int uninit_42(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 715;
    p->f1 = 718;
    int v;
    switch (p->f1) { case 3: v = p->f2; break; default: break; }
    free(p);
    return v;
}

/* CASE 43 */
int uninit_43(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 732;
    p->f1 = 735;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 44 */
int uninit_44(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 749;
    p->f1 = 752;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 45 */
int uninit_45(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 766;
    p->f1 = 769;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 46 */
int uninit_46(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 783;
    p->f1 = 786;
    int v;
    switch (p->f1) { case 2: v = p->f2; break; default: break; }
    free(p);
    return v;
}

/* CASE 47 */
int uninit_47(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 800;
    p->f1 = 803;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 48 */
int uninit_48(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 817;
    p->f1 = 820;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 49 */
int uninit_49(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 50 */
int uninit_50(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 851;
    int v;
    switch (p->f0) { case 1: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 51 */
int uninit_51(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 52 */
int uninit_52(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 885;
    p->f1 = 888;
    p->f2 = 891;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 53 */
int uninit_53(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 902;
    p->f1 = 905;
    p->f2 = 908;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 54 */
int uninit_54(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 919;
    p->f1 = 922;
    p->f2 = 925;
    int v;
    switch (p->f2) { case 5: v = p->f3; break; default: break; }
    free(p);
    return v;
}

/* CASE 55 */
int uninit_55(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 936;
    p->f1 = 939;
    p->f2 = 942;
    int v;
    if (p->f2) { if (p->f2 != 0) v = p->f3; }
    free(p);
    return v;
}

/* CASE 56 */
int uninit_56(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 953;
    p->f1 = 956;
    p->f2 = 959;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 57 */
int uninit_57(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 970;
    p->f1 = 973;
    p->f2 = 976;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 58 */
int uninit_58(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 987;
    p->f1 = 990;
    p->f2 = 993;
    int v;
    switch (p->f2) { case 4: v = p->f3; break; default: break; }
    free(p);
    return v;
}

/* CASE 59 */
int uninit_59(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 1004;
    p->f1 = 1007;
    p->f2 = 1010;
    int v;
    if (p->f2) { if (p->f2 != 0) v = p->f3; }
    free(p);
    return v;
}

/* CASE 60 */
int uninit_60(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 1021;
    p->f1 = 1024;
    p->f2 = 1027;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 61 */
int uninit_61(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 1038;
    p->f1 = 1041;
    p->f2 = 1044;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 62 */
int uninit_62(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 1055;
    p->f1 = 1058;
    p->f2 = 1061;
    int v;
    switch (p->f2) { case 3: v = p->f3; break; default: break; }
    free(p);
    return v;
}

/* CASE 63 */
int uninit_63(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 1072;
    p->f1 = 1075;
    p->f2 = 1078;
    int v;
    if (p->f2) { if (p->f2 != 0) v = p->f3; }
    free(p);
    return v;
}

/* CASE 64 */
int uninit_64(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 1089;
    p->f1 = 1092;
    p->f2 = 1095;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 65 */
int uninit_65(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 66 */
int uninit_66(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 2: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 67 */
int uninit_67(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 1140;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 68 */
int uninit_68(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 69 */
int uninit_69(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 1174;
    p->f1 = 1177;
    p->f2 = 1180;
    p->f3 = 1183;
    int v;
    if (p->f3) v = p->f4;
    free(p);
    return v;
}

/* CASE 70 */
int uninit_70(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 1191;
    p->f1 = 1194;
    p->f2 = 1197;
    p->f3 = 1200;
    int v;
    switch (p->f3) { case 1: v = p->f4; break; default: break; }
    free(p);
    return v;
}

/* CASE 71 */
int uninit_71(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 1208;
    p->f1 = 1211;
    p->f2 = 1214;
    p->f3 = 1217;
    int v;
    if (p->f3) { if (p->f3 != 0) v = p->f4; }
    free(p);
    return v;
}

/* CASE 72 */
int uninit_72(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 1225;
    p->f1 = 1228;
    p->f2 = 1231;
    p->f3 = 1234;
    int v;
    if (p->f3) v = p->f4;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 73 */
int uninit_73(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 1242;
    p->f1 = 1245;
    p->f2 = 1248;
    p->f3 = 1251;
    int v;
    if (p->f3) v = p->f4;
    free(p);
    return v;
}

/* CASE 74 */
int uninit_74(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 1259;
    p->f1 = 1262;
    p->f2 = 1265;
    p->f3 = 1268;
    int v;
    switch (p->f3) { case 5: v = p->f4; break; default: break; }
    free(p);
    return v;
}

/* CASE 75 */
int uninit_75(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 1276;
    p->f1 = 1279;
    p->f2 = 1282;
    p->f3 = 1285;
    int v;
    if (p->f3) { if (p->f3 != 0) v = p->f4; }
    free(p);
    return v;
}

/* CASE 76 */
int uninit_76(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 1293;
    p->f1 = 1296;
    p->f2 = 1299;
    p->f3 = 1302;
    int v;
    if (p->f3) v = p->f4;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 77 */
int uninit_77(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 1310;
    p->f1 = 1313;
    p->f2 = 1316;
    p->f3 = 1319;
    int v;
    if (p->f3) v = p->f4;
    free(p);
    return v;
}

/* CASE 78 */
int uninit_78(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 1327;
    p->f1 = 1330;
    p->f2 = 1333;
    p->f3 = 1336;
    int v;
    switch (p->f3) { case 4: v = p->f4; break; default: break; }
    free(p);
    return v;
}

/* CASE 79 */
int uninit_79(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 1344;
    p->f1 = 1347;
    p->f2 = 1350;
    p->f3 = 1353;
    int v;
    if (p->f3) { if (p->f3 != 0) v = p->f4; }
    free(p);
    return v;
}

/* CASE 80 */
int uninit_80(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 1361;
    p->f1 = 1364;
    p->f2 = 1367;
    p->f3 = 1370;
    int v;
    if (p->f3) v = p->f4;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 81 */
int uninit_81(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 82 */
int uninit_82(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 1395;
    int v;
    switch (p->f0) { case 3: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 83 */
int uninit_83(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 1412;
    p->f1 = 1415;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 84 */
int uninit_84(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 1429;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 85 */
int uninit_85(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 86 */
int uninit_86(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 1463;
    p->f1 = 1466;
    p->f2 = 1469;
    p->f3 = 1472;
    p->f4 = 1475;
    int v;
    switch (p->f4) { case 2: v = p->f5; break; default: break; }
    free(p);
    return v;
}

/* CASE 87 */
int uninit_87(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 1480;
    p->f1 = 1483;
    p->f2 = 1486;
    p->f3 = 1489;
    p->f4 = 1492;
    int v;
    if (p->f4) { if (p->f4 != 0) v = p->f5; }
    free(p);
    return v;
}

/* CASE 88 */
int uninit_88(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 1497;
    p->f1 = 1500;
    p->f2 = 1503;
    p->f3 = 1506;
    p->f4 = 1509;
    int v;
    if (p->f4) v = p->f5;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 89 */
int uninit_89(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 1514;
    p->f1 = 1517;
    p->f2 = 1520;
    p->f3 = 1523;
    p->f4 = 1526;
    int v;
    if (p->f4) v = p->f5;
    free(p);
    return v;
}

/* CASE 90 */
int uninit_90(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 1531;
    p->f1 = 1534;
    p->f2 = 1537;
    p->f3 = 1540;
    p->f4 = 1543;
    int v;
    switch (p->f4) { case 1: v = p->f5; break; default: break; }
    free(p);
    return v;
}

/* CASE 91 */
int uninit_91(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 1548;
    p->f1 = 1551;
    p->f2 = 1554;
    p->f3 = 1557;
    p->f4 = 1560;
    int v;
    if (p->f4) { if (p->f4 != 0) v = p->f5; }
    free(p);
    return v;
}

/* CASE 92 */
int uninit_92(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 1565;
    p->f1 = 1568;
    p->f2 = 1571;
    p->f3 = 1574;
    p->f4 = 1577;
    int v;
    if (p->f4) v = p->f5;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 93 */
int uninit_93(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 1582;
    p->f1 = 1585;
    p->f2 = 1588;
    p->f3 = 1591;
    p->f4 = 1594;
    int v;
    if (p->f4) v = p->f5;
    free(p);
    return v;
}

/* CASE 94 */
int uninit_94(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 1599;
    p->f1 = 1602;
    p->f2 = 1605;
    p->f3 = 1608;
    p->f4 = 1611;
    int v;
    switch (p->f4) { case 5: v = p->f5; break; default: break; }
    free(p);
    return v;
}

/* CASE 95 */
int uninit_95(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 1616;
    p->f1 = 1619;
    p->f2 = 1622;
    p->f3 = 1625;
    p->f4 = 1628;
    int v;
    if (p->f4) { if (p->f4 != 0) v = p->f5; }
    free(p);
    return v;
}

/* CASE 96 */
int uninit_96(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 1633;
    p->f1 = 1636;
    p->f2 = 1639;
    p->f3 = 1642;
    p->f4 = 1645;
    int v;
    if (p->f4) v = p->f5;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 97 */
int uninit_97(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 98 */
int uninit_98(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 4: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 99 */
int uninit_99(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 100 */
int uninit_100(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 1701;
    p->f1 = 1704;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 101 */
int uninit_101(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 1718;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 102 */
int uninit_102(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 3: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 103 */
int uninit_103(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 1752;
    p->f1 = 1755;
    p->f2 = 1758;
    p->f3 = 1761;
    p->f4 = 1764;
    p->f5 = 1767;
    int v;
    if (p->f5) { if (p->f5 != 0) v = p->f6; }
    free(p);
    return v;
}

/* CASE 104 */
int uninit_104(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 1769;
    p->f1 = 1772;
    p->f2 = 1775;
    p->f3 = 1778;
    p->f4 = 1781;
    p->f5 = 1784;
    int v;
    if (p->f5) v = p->f6;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 105 */
int uninit_105(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 1786;
    p->f1 = 1789;
    p->f2 = 1792;
    p->f3 = 1795;
    p->f4 = 1798;
    p->f5 = 1801;
    int v;
    if (p->f5) v = p->f6;
    free(p);
    return v;
}

/* CASE 106 */
int uninit_106(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 1803;
    p->f1 = 1806;
    p->f2 = 1809;
    p->f3 = 1812;
    p->f4 = 1815;
    p->f5 = 1818;
    int v;
    switch (p->f5) { case 2: v = p->f6; break; default: break; }
    free(p);
    return v;
}

/* CASE 107 */
int uninit_107(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 1820;
    p->f1 = 1823;
    p->f2 = 1826;
    p->f3 = 1829;
    p->f4 = 1832;
    p->f5 = 1835;
    int v;
    if (p->f5) { if (p->f5 != 0) v = p->f6; }
    free(p);
    return v;
}

/* CASE 108 */
int uninit_108(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 1837;
    p->f1 = 1840;
    p->f2 = 1843;
    p->f3 = 1846;
    p->f4 = 1849;
    p->f5 = 1852;
    int v;
    if (p->f5) v = p->f6;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 109 */
int uninit_109(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 1854;
    p->f1 = 1857;
    p->f2 = 1860;
    p->f3 = 1863;
    p->f4 = 1866;
    p->f5 = 1869;
    int v;
    if (p->f5) v = p->f6;
    free(p);
    return v;
}

/* CASE 110 */
int uninit_110(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 1871;
    p->f1 = 1874;
    p->f2 = 1877;
    p->f3 = 1880;
    p->f4 = 1883;
    p->f5 = 1886;
    int v;
    switch (p->f5) { case 1: v = p->f6; break; default: break; }
    free(p);
    return v;
}

/* CASE 111 */
int uninit_111(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 1888;
    p->f1 = 1891;
    p->f2 = 1894;
    p->f3 = 1897;
    p->f4 = 1900;
    p->f5 = 1903;
    int v;
    if (p->f5) { if (p->f5 != 0) v = p->f6; }
    free(p);
    return v;
}

/* CASE 112 */
int uninit_112(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 1905;
    p->f1 = 1908;
    p->f2 = 1911;
    p->f3 = 1914;
    p->f4 = 1917;
    p->f5 = 1920;
    int v;
    if (p->f5) v = p->f6;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 113 */
int uninit_113(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 114 */
int uninit_114(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 1939;
    int v;
    switch (p->f0) { case 5: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 115 */
int uninit_115(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 1956;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 116 */
int uninit_116(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 1973;
    p->f1 = 1976;
    p->f2 = 1979;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 117 */
int uninit_117(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 1990;
    p->f1 = 1993;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 118 */
int uninit_118(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 2007;
    int v;
    switch (p->f0) { case 4: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 119 */
int uninit_119(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 120 */
int uninit_120(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 2041;
    p->f1 = 2044;
    p->f2 = 2047;
    p->f3 = 2050;
    p->f4 = 2053;
    p->f5 = 2056;
    p->f6 = 2059;
    int v;
    if (p->f6) v = p->f7;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 121 */
int uninit_121(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 2058;
    p->f1 = 2061;
    p->f2 = 2064;
    p->f3 = 2067;
    p->f4 = 2070;
    p->f5 = 2073;
    p->f6 = 2076;
    int v;
    if (p->f6) v = p->f7;
    free(p);
    return v;
}

/* CASE 122 */
int uninit_122(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 2075;
    p->f1 = 2078;
    p->f2 = 2081;
    p->f3 = 2084;
    p->f4 = 2087;
    p->f5 = 2090;
    p->f6 = 2093;
    int v;
    switch (p->f6) { case 3: v = p->f7; break; default: break; }
    free(p);
    return v;
}

/* CASE 123 */
int uninit_123(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 2092;
    p->f1 = 2095;
    p->f2 = 2098;
    p->f3 = 2101;
    p->f4 = 2104;
    p->f5 = 2107;
    p->f6 = 2110;
    int v;
    if (p->f6) { if (p->f6 != 0) v = p->f7; }
    free(p);
    return v;
}

/* CASE 124 */
int uninit_124(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 2109;
    p->f1 = 2112;
    p->f2 = 2115;
    p->f3 = 2118;
    p->f4 = 2121;
    p->f5 = 2124;
    p->f6 = 2127;
    int v;
    if (p->f6) v = p->f7;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 125 */
int uninit_125(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 2126;
    p->f1 = 2129;
    p->f2 = 2132;
    p->f3 = 2135;
    p->f4 = 2138;
    p->f5 = 2141;
    p->f6 = 2144;
    int v;
    if (p->f6) v = p->f7;
    free(p);
    return v;
}

/* CASE 126 */
int uninit_126(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 2143;
    p->f1 = 2146;
    p->f2 = 2149;
    p->f3 = 2152;
    p->f4 = 2155;
    p->f5 = 2158;
    p->f6 = 2161;
    int v;
    switch (p->f6) { case 2: v = p->f7; break; default: break; }
    free(p);
    return v;
}

/* CASE 127 */
int uninit_127(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 2160;
    p->f1 = 2163;
    p->f2 = 2166;
    p->f3 = 2169;
    p->f4 = 2172;
    p->f5 = 2175;
    p->f6 = 2178;
    int v;
    if (p->f6) { if (p->f6 != 0) v = p->f7; }
    free(p);
    return v;
}

/* CASE 128 */
int uninit_128(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 2177;
    p->f1 = 2180;
    p->f2 = 2183;
    p->f3 = 2186;
    p->f4 = 2189;
    p->f5 = 2192;
    p->f6 = 2195;
    int v;
    if (p->f6) v = p->f7;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 129 */
int uninit_129(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 130 */
int uninit_130(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 1: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 131 */
int uninit_131(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 2228;
    p->f1 = 2231;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 132 */
int uninit_132(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 133 */
int uninit_133(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 2262;
    p->f1 = 2265;
    p->f2 = 2268;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 134 */
int uninit_134(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 2279;
    p->f1 = 2282;
    int v;
    switch (p->f1) { case 5: v = p->f2; break; default: break; }
    free(p);
    return v;
}

/* CASE 135 */
int uninit_135(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 2296;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 136 */
int uninit_136(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 137 */
int uninit_137(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 2330;
    p->f1 = 2333;
    p->f2 = 2336;
    p->f3 = 2339;
    p->f4 = 2342;
    p->f5 = 2345;
    p->f6 = 2348;
    p->f7 = 2351;
    int v;
    if (p->f7) v = p->f8;
    free(p);
    return v;
}

/* CASE 138 */
int uninit_138(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 2347;
    p->f1 = 2350;
    p->f2 = 2353;
    p->f3 = 2356;
    p->f4 = 2359;
    p->f5 = 2362;
    p->f6 = 2365;
    p->f7 = 2368;
    int v;
    switch (p->f7) { case 4: v = p->f8; break; default: break; }
    free(p);
    return v;
}

/* CASE 139 */
int uninit_139(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 2364;
    p->f1 = 2367;
    p->f2 = 2370;
    p->f3 = 2373;
    p->f4 = 2376;
    p->f5 = 2379;
    p->f6 = 2382;
    p->f7 = 2385;
    int v;
    if (p->f7) { if (p->f7 != 0) v = p->f8; }
    free(p);
    return v;
}

/* CASE 140 */
int uninit_140(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 2381;
    p->f1 = 2384;
    p->f2 = 2387;
    p->f3 = 2390;
    p->f4 = 2393;
    p->f5 = 2396;
    p->f6 = 2399;
    p->f7 = 2402;
    int v;
    if (p->f7) v = p->f8;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 141 */
int uninit_141(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 2398;
    p->f1 = 2401;
    p->f2 = 2404;
    p->f3 = 2407;
    p->f4 = 2410;
    p->f5 = 2413;
    p->f6 = 2416;
    p->f7 = 2419;
    int v;
    if (p->f7) v = p->f8;
    free(p);
    return v;
}

/* CASE 142 */
int uninit_142(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 2415;
    p->f1 = 2418;
    p->f2 = 2421;
    p->f3 = 2424;
    p->f4 = 2427;
    p->f5 = 2430;
    p->f6 = 2433;
    p->f7 = 2436;
    int v;
    switch (p->f7) { case 3: v = p->f8; break; default: break; }
    free(p);
    return v;
}

/* CASE 143 */
int uninit_143(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 2432;
    p->f1 = 2435;
    p->f2 = 2438;
    p->f3 = 2441;
    p->f4 = 2444;
    p->f5 = 2447;
    p->f6 = 2450;
    p->f7 = 2453;
    int v;
    if (p->f7) { if (p->f7 != 0) v = p->f8; }
    free(p);
    return v;
}

/* CASE 144 */
int uninit_144(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 2449;
    p->f1 = 2452;
    p->f2 = 2455;
    p->f3 = 2458;
    p->f4 = 2461;
    p->f5 = 2464;
    p->f6 = 2467;
    p->f7 = 2470;
    int v;
    if (p->f7) v = p->f8;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 145 */
int uninit_145(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 146 */
int uninit_146(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 2483;
    int v;
    switch (p->f0) { case 2: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 147 */
int uninit_147(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 148 */
int uninit_148(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 2517;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 149 */
int uninit_149(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 2534;
    p->f1 = 2537;
    p->f2 = 2540;
    p->f3 = 2543;
    int v;
    if (p->f3) v = p->f4;
    free(p);
    return v;
}

/* CASE 150 */
int uninit_150(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 2551;
    p->f1 = 2554;
    p->f2 = 2557;
    int v;
    switch (p->f2) { case 1: v = p->f3; break; default: break; }
    free(p);
    return v;
}

/* CASE 151 */
int uninit_151(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 2568;
    p->f1 = 2571;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 152 */
int uninit_152(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 2585;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 153 */
int uninit_153(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 154 */
int uninit_154(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 2619;
    p->f1 = 2622;
    p->f2 = 2625;
    p->f3 = 2628;
    p->f4 = 2631;
    p->f5 = 2634;
    p->f6 = 2637;
    p->f7 = 2640;
    p->f8 = 2643;
    int v;
    switch (p->f8) { case 5: v = p->f9; break; default: break; }
    free(p);
    return v;
}

/* CASE 155 */
int uninit_155(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 2636;
    p->f1 = 2639;
    p->f2 = 2642;
    p->f3 = 2645;
    p->f4 = 2648;
    p->f5 = 2651;
    p->f6 = 2654;
    p->f7 = 2657;
    p->f8 = 2660;
    int v;
    if (p->f8) { if (p->f8 != 0) v = p->f9; }
    free(p);
    return v;
}

/* CASE 156 */
int uninit_156(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 2653;
    p->f1 = 2656;
    p->f2 = 2659;
    p->f3 = 2662;
    p->f4 = 2665;
    p->f5 = 2668;
    p->f6 = 2671;
    p->f7 = 2674;
    p->f8 = 2677;
    int v;
    if (p->f8) v = p->f9;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 157 */
int uninit_157(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 2670;
    p->f1 = 2673;
    p->f2 = 2676;
    p->f3 = 2679;
    p->f4 = 2682;
    p->f5 = 2685;
    p->f6 = 2688;
    p->f7 = 2691;
    p->f8 = 2694;
    int v;
    if (p->f8) v = p->f9;
    free(p);
    return v;
}

/* CASE 158 */
int uninit_158(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 2687;
    p->f1 = 2690;
    p->f2 = 2693;
    p->f3 = 2696;
    p->f4 = 2699;
    p->f5 = 2702;
    p->f6 = 2705;
    p->f7 = 2708;
    p->f8 = 2711;
    int v;
    switch (p->f8) { case 4: v = p->f9; break; default: break; }
    free(p);
    return v;
}

/* CASE 159 */
int uninit_159(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 2704;
    p->f1 = 2707;
    p->f2 = 2710;
    p->f3 = 2713;
    p->f4 = 2716;
    p->f5 = 2719;
    p->f6 = 2722;
    p->f7 = 2725;
    p->f8 = 2728;
    int v;
    if (p->f8) { if (p->f8 != 0) v = p->f9; }
    free(p);
    return v;
}

/* CASE 160 */
int uninit_160(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 2721;
    p->f1 = 2724;
    p->f2 = 2727;
    p->f3 = 2730;
    p->f4 = 2733;
    p->f5 = 2736;
    p->f6 = 2739;
    p->f7 = 2742;
    p->f8 = 2745;
    int v;
    if (p->f8) v = p->f9;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 161 */
int uninit_161(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 162 */
int uninit_162(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 3: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 163 */
int uninit_163(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 2772;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 164 */
int uninit_164(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 2789;
    p->f1 = 2792;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 165 */
int uninit_165(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 166 */
int uninit_166(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 2823;
    p->f1 = 2826;
    p->f2 = 2829;
    p->f3 = 2832;
    int v;
    switch (p->f3) { case 2: v = p->f4; break; default: break; }
    free(p);
    return v;
}

/* CASE 167 */
int uninit_167(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 2840;
    p->f1 = 2843;
    p->f2 = 2846;
    int v;
    if (p->f2) { if (p->f2 != 0) v = p->f3; }
    free(p);
    return v;
}

/* CASE 168 */
int uninit_168(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 2857;
    p->f1 = 2860;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 169 */
int uninit_169(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 2874;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 170 */
int uninit_170(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 1: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 171 */
int uninit_171(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 2908;
    p->f1 = 2911;
    p->f2 = 2914;
    p->f3 = 2917;
    p->f4 = 2920;
    p->f5 = 2923;
    p->f6 = 2926;
    p->f7 = 2929;
    p->f8 = 2932;
    p->f9 = 2935;
    int v;
    if (p->f9) { if (p->f9 != 0) v = p->f10; }
    free(p);
    return v;
}

/* CASE 172 */
int uninit_172(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 2925;
    p->f1 = 2928;
    p->f2 = 2931;
    p->f3 = 2934;
    p->f4 = 2937;
    p->f5 = 2940;
    p->f6 = 2943;
    p->f7 = 2946;
    p->f8 = 2949;
    p->f9 = 2952;
    int v;
    if (p->f9) v = p->f10;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 173 */
int uninit_173(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 2942;
    p->f1 = 2945;
    p->f2 = 2948;
    p->f3 = 2951;
    p->f4 = 2954;
    p->f5 = 2957;
    p->f6 = 2960;
    p->f7 = 2963;
    p->f8 = 2966;
    p->f9 = 2969;
    int v;
    if (p->f9) v = p->f10;
    free(p);
    return v;
}

/* CASE 174 */
int uninit_174(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 2959;
    p->f1 = 2962;
    p->f2 = 2965;
    p->f3 = 2968;
    p->f4 = 2971;
    p->f5 = 2974;
    p->f6 = 2977;
    p->f7 = 2980;
    p->f8 = 2983;
    p->f9 = 2986;
    int v;
    switch (p->f9) { case 5: v = p->f10; break; default: break; }
    free(p);
    return v;
}

/* CASE 175 */
int uninit_175(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 2976;
    p->f1 = 2979;
    p->f2 = 2982;
    p->f3 = 2985;
    p->f4 = 2988;
    p->f5 = 2991;
    p->f6 = 2994;
    p->f7 = 2997;
    p->f8 = 3000;
    p->f9 = 3003;
    int v;
    if (p->f9) { if (p->f9 != 0) v = p->f10; }
    free(p);
    return v;
}

/* CASE 176 */
int uninit_176(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 2993;
    p->f1 = 2996;
    p->f2 = 2999;
    p->f3 = 3002;
    p->f4 = 3005;
    p->f5 = 3008;
    p->f6 = 3011;
    p->f7 = 3014;
    p->f8 = 3017;
    p->f9 = 3020;
    int v;
    if (p->f9) v = p->f10;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 177 */
int uninit_177(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 178 */
int uninit_178(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 3027;
    int v;
    switch (p->f0) { case 4: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 179 */
int uninit_179(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 3044;
    p->f1 = 3047;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 180 */
int uninit_180(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 3061;
    p->f1 = 3064;
    p->f2 = 3067;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 181 */
int uninit_181(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 3078;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 182 */
int uninit_182(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 3095;
    p->f1 = 3098;
    p->f2 = 3101;
    p->f3 = 3104;
    p->f4 = 3107;
    int v;
    switch (p->f4) { case 3: v = p->f5; break; default: break; }
    free(p);
    return v;
}

/* CASE 183 */
int uninit_183(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 3112;
    p->f1 = 3115;
    p->f2 = 3118;
    p->f3 = 3121;
    int v;
    if (p->f3) { if (p->f3 != 0) v = p->f4; }
    free(p);
    return v;
}

/* CASE 184 */
int uninit_184(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 3129;
    p->f1 = 3132;
    p->f2 = 3135;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 185 */
int uninit_185(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 3146;
    p->f1 = 3149;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 186 */
int uninit_186(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 3163;
    int v;
    switch (p->f0) { case 2: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 187 */
int uninit_187(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 188 */
int uninit_188(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 3197;
    p->f1 = 3200;
    p->f2 = 3203;
    p->f3 = 3206;
    p->f4 = 3209;
    p->f5 = 3212;
    p->f6 = 3215;
    p->f7 = 3218;
    p->f8 = 3221;
    p->f9 = 3224;
    p->f10 = 3227;
    int v;
    if (p->f10) v = p->f11;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 189 */
int uninit_189(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 3214;
    p->f1 = 3217;
    p->f2 = 3220;
    p->f3 = 3223;
    p->f4 = 3226;
    p->f5 = 3229;
    p->f6 = 3232;
    p->f7 = 3235;
    p->f8 = 3238;
    p->f9 = 3241;
    p->f10 = 3244;
    int v;
    if (p->f10) v = p->f11;
    free(p);
    return v;
}

/* CASE 190 */
int uninit_190(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 3231;
    p->f1 = 3234;
    p->f2 = 3237;
    p->f3 = 3240;
    p->f4 = 3243;
    p->f5 = 3246;
    p->f6 = 3249;
    p->f7 = 3252;
    p->f8 = 3255;
    p->f9 = 3258;
    p->f10 = 3261;
    int v;
    switch (p->f10) { case 1: v = p->f11; break; default: break; }
    free(p);
    return v;
}

/* CASE 191 */
int uninit_191(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 3248;
    p->f1 = 3251;
    p->f2 = 3254;
    p->f3 = 3257;
    p->f4 = 3260;
    p->f5 = 3263;
    p->f6 = 3266;
    p->f7 = 3269;
    p->f8 = 3272;
    p->f9 = 3275;
    p->f10 = 3278;
    int v;
    if (p->f10) { if (p->f10 != 0) v = p->f11; }
    free(p);
    return v;
}

/* CASE 192 */
int uninit_192(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 3265;
    p->f1 = 3268;
    p->f2 = 3271;
    p->f3 = 3274;
    p->f4 = 3277;
    p->f5 = 3280;
    p->f6 = 3283;
    p->f7 = 3286;
    p->f8 = 3289;
    p->f9 = 3292;
    p->f10 = 3295;
    int v;
    if (p->f10) v = p->f11;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 193 */
int uninit_193(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 194 */
int uninit_194(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 5: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 195 */
int uninit_195(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 196 */
int uninit_196(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 197 */
int uninit_197(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 3350;
    p->f1 = 3353;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 198 */
int uninit_198(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 4: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 199 */
int uninit_199(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 3384;
    p->f1 = 3387;
    p->f2 = 3390;
    p->f3 = 3393;
    p->f4 = 3396;
    int v;
    if (p->f4) { if (p->f4 != 0) v = p->f5; }
    free(p);
    return v;
}

/* CASE 200 */
int uninit_200(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 3401;
    p->f1 = 3404;
    p->f2 = 3407;
    p->f3 = 3410;
    int v;
    if (p->f3) v = p->f4;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 201 */
int uninit_201(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 3418;
    p->f1 = 3421;
    p->f2 = 3424;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 202 */
int uninit_202(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 3435;
    p->f1 = 3438;
    int v;
    switch (p->f1) { case 3: v = p->f2; break; default: break; }
    free(p);
    return v;
}

/* CASE 203 */
int uninit_203(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 3452;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 204 */
int uninit_204(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 205 */
int uninit_205(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 3486;
    p->f1 = 3489;
    p->f2 = 3492;
    p->f3 = 3495;
    p->f4 = 3498;
    p->f5 = 3501;
    p->f6 = 3504;
    p->f7 = 3507;
    p->f8 = 3510;
    p->f9 = 3513;
    p->f10 = 3516;
    p->f11 = 3519;
    int v;
    if (p->f11) v = p->f12;
    free(p);
    return v;
}

/* CASE 206 */
int uninit_206(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 3503;
    p->f1 = 3506;
    p->f2 = 3509;
    p->f3 = 3512;
    p->f4 = 3515;
    p->f5 = 3518;
    p->f6 = 3521;
    p->f7 = 3524;
    p->f8 = 3527;
    p->f9 = 3530;
    p->f10 = 3533;
    p->f11 = 3536;
    int v;
    switch (p->f11) { case 2: v = p->f12; break; default: break; }
    free(p);
    return v;
}

/* CASE 207 */
int uninit_207(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 3520;
    p->f1 = 3523;
    p->f2 = 3526;
    p->f3 = 3529;
    p->f4 = 3532;
    p->f5 = 3535;
    p->f6 = 3538;
    p->f7 = 3541;
    p->f8 = 3544;
    p->f9 = 3547;
    p->f10 = 3550;
    p->f11 = 3553;
    int v;
    if (p->f11) { if (p->f11 != 0) v = p->f12; }
    free(p);
    return v;
}

/* CASE 208 */
int uninit_208(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 3537;
    p->f1 = 3540;
    p->f2 = 3543;
    p->f3 = 3546;
    p->f4 = 3549;
    p->f5 = 3552;
    p->f6 = 3555;
    p->f7 = 3558;
    p->f8 = 3561;
    p->f9 = 3564;
    p->f10 = 3567;
    p->f11 = 3570;
    int v;
    if (p->f11) v = p->f12;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 209 */
int uninit_209(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 210 */
int uninit_210(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 3571;
    int v;
    switch (p->f0) { case 1: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 211 */
int uninit_211(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 3588;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 212 */
int uninit_212(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 3605;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 213 */
int uninit_213(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 3622;
    p->f1 = 3625;
    p->f2 = 3628;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 214 */
int uninit_214(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 3639;
    int v;
    switch (p->f0) { case 5: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 215 */
int uninit_215(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 3656;
    p->f1 = 3659;
    p->f2 = 3662;
    p->f3 = 3665;
    p->f4 = 3668;
    p->f5 = 3671;
    int v;
    if (p->f5) { if (p->f5 != 0) v = p->f6; }
    free(p);
    return v;
}

/* CASE 216 */
int uninit_216(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 3673;
    p->f1 = 3676;
    p->f2 = 3679;
    p->f3 = 3682;
    p->f4 = 3685;
    int v;
    if (p->f4) v = p->f5;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 217 */
int uninit_217(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 3690;
    p->f1 = 3693;
    p->f2 = 3696;
    p->f3 = 3699;
    int v;
    if (p->f3) v = p->f4;
    free(p);
    return v;
}

/* CASE 218 */
int uninit_218(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 3707;
    p->f1 = 3710;
    p->f2 = 3713;
    int v;
    switch (p->f2) { case 4: v = p->f3; break; default: break; }
    free(p);
    return v;
}

/* CASE 219 */
int uninit_219(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 3724;
    p->f1 = 3727;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 220 */
int uninit_220(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 3741;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 221 */
int uninit_221(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 222 */
int uninit_222(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 3775;
    p->f1 = 3778;
    p->f2 = 3781;
    p->f3 = 3784;
    p->f4 = 3787;
    p->f5 = 3790;
    p->f6 = 3793;
    p->f7 = 3796;
    p->f8 = 3799;
    p->f9 = 3802;
    p->f10 = 3805;
    p->f11 = 3808;
    p->f12 = 3811;
    int v;
    switch (p->f12) { case 3: v = p->f13; break; default: break; }
    free(p);
    return v;
}

/* CASE 223 */
int uninit_223(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 3792;
    p->f1 = 3795;
    p->f2 = 3798;
    p->f3 = 3801;
    p->f4 = 3804;
    p->f5 = 3807;
    p->f6 = 3810;
    p->f7 = 3813;
    p->f8 = 3816;
    p->f9 = 3819;
    p->f10 = 3822;
    p->f11 = 3825;
    p->f12 = 3828;
    int v;
    if (p->f12) { if (p->f12 != 0) v = p->f13; }
    free(p);
    return v;
}

/* CASE 224 */
int uninit_224(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 3809;
    p->f1 = 3812;
    p->f2 = 3815;
    p->f3 = 3818;
    p->f4 = 3821;
    p->f5 = 3824;
    p->f6 = 3827;
    p->f7 = 3830;
    p->f8 = 3833;
    p->f9 = 3836;
    p->f10 = 3839;
    p->f11 = 3842;
    p->f12 = 3845;
    int v;
    if (p->f12) v = p->f13;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 225 */
int uninit_225(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 226 */
int uninit_226(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 2: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 227 */
int uninit_227(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 3860;
    p->f1 = 3863;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 228 */
int uninit_228(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 3877;
    p->f1 = 3880;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 229 */
int uninit_229(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 3894;
    p->f1 = 3897;
    p->f2 = 3900;
    p->f3 = 3903;
    int v;
    if (p->f3) v = p->f4;
    free(p);
    return v;
}

/* CASE 230 */
int uninit_230(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 3911;
    p->f1 = 3914;
    int v;
    switch (p->f1) { case 1: v = p->f2; break; default: break; }
    free(p);
    return v;
}

/* CASE 231 */
int uninit_231(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 232 */
int uninit_232(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 3945;
    p->f1 = 3948;
    p->f2 = 3951;
    p->f3 = 3954;
    p->f4 = 3957;
    p->f5 = 3960;
    int v;
    if (p->f5) v = p->f6;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 233 */
int uninit_233(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 3962;
    p->f1 = 3965;
    p->f2 = 3968;
    p->f3 = 3971;
    p->f4 = 3974;
    int v;
    if (p->f4) v = p->f5;
    free(p);
    return v;
}

/* CASE 234 */
int uninit_234(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 3979;
    p->f1 = 3982;
    p->f2 = 3985;
    p->f3 = 3988;
    int v;
    switch (p->f3) { case 5: v = p->f4; break; default: break; }
    free(p);
    return v;
}

/* CASE 235 */
int uninit_235(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 3996;
    p->f1 = 3999;
    p->f2 = 4002;
    int v;
    if (p->f2) { if (p->f2 != 0) v = p->f3; }
    free(p);
    return v;
}

/* CASE 236 */
int uninit_236(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 4013;
    p->f1 = 4016;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 237 */
int uninit_237(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 4030;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 238 */
int uninit_238(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 4: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 239 */
int uninit_239(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 4064;
    p->f1 = 4067;
    p->f2 = 4070;
    p->f3 = 4073;
    p->f4 = 4076;
    p->f5 = 4079;
    p->f6 = 4082;
    p->f7 = 4085;
    p->f8 = 4088;
    p->f9 = 4091;
    p->f10 = 4094;
    p->f11 = 4097;
    p->f12 = 4100;
    p->f13 = 4103;
    int v;
    if (p->f13) { if (p->f13 != 0) v = p->f14; }
    free(p);
    return v;
}

/* CASE 240 */
int uninit_240(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 4081;
    p->f1 = 4084;
    p->f2 = 4087;
    p->f3 = 4090;
    p->f4 = 4093;
    p->f5 = 4096;
    p->f6 = 4099;
    p->f7 = 4102;
    p->f8 = 4105;
    p->f9 = 4108;
    p->f10 = 4111;
    p->f11 = 4114;
    p->f12 = 4117;
    p->f13 = 4120;
    int v;
    if (p->f13) v = p->f14;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 241 */
int uninit_241(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 242 */
int uninit_242(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 4115;
    int v;
    switch (p->f0) { case 3: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 243 */
int uninit_243(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 244 */
int uninit_244(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 4149;
    p->f1 = 4152;
    p->f2 = 4155;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 245 */
int uninit_245(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 246 */
int uninit_246(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 4183;
    p->f1 = 4186;
    p->f2 = 4189;
    int v;
    switch (p->f2) { case 2: v = p->f3; break; default: break; }
    free(p);
    return v;
}

/* CASE 247 */
int uninit_247(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 4200;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 248 */
int uninit_248(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 4217;
    p->f1 = 4220;
    p->f2 = 4223;
    p->f3 = 4226;
    p->f4 = 4229;
    p->f5 = 4232;
    p->f6 = 4235;
    int v;
    if (p->f6) v = p->f7;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 249 */
int uninit_249(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 4234;
    p->f1 = 4237;
    p->f2 = 4240;
    p->f3 = 4243;
    p->f4 = 4246;
    p->f5 = 4249;
    int v;
    if (p->f5) v = p->f6;
    free(p);
    return v;
}

/* CASE 250 */
int uninit_250(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 4251;
    p->f1 = 4254;
    p->f2 = 4257;
    p->f3 = 4260;
    p->f4 = 4263;
    int v;
    switch (p->f4) { case 1: v = p->f5; break; default: break; }
    free(p);
    return v;
}

/* CASE 251 */
int uninit_251(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 4268;
    p->f1 = 4271;
    p->f2 = 4274;
    p->f3 = 4277;
    int v;
    if (p->f3) { if (p->f3 != 0) v = p->f4; }
    free(p);
    return v;
}

/* CASE 252 */
int uninit_252(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 4285;
    p->f1 = 4288;
    p->f2 = 4291;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 253 */
int uninit_253(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 4302;
    p->f1 = 4305;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 254 */
int uninit_254(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 4319;
    int v;
    switch (p->f0) { case 5: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 255 */
int uninit_255(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 256 */
int uninit_256(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 4353;
    p->f1 = 4356;
    p->f2 = 4359;
    p->f3 = 4362;
    p->f4 = 4365;
    p->f5 = 4368;
    p->f6 = 4371;
    p->f7 = 4374;
    p->f8 = 4377;
    p->f9 = 4380;
    p->f10 = 4383;
    p->f11 = 4386;
    p->f12 = 4389;
    p->f13 = 4392;
    p->f14 = 4395;
    int v;
    if (p->f14) v = p->f15;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 257 */
int uninit_257(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 258 */
int uninit_258(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 4: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 259 */
int uninit_259(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 4404;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 260 */
int uninit_260(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 261 */
int uninit_261(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 4438;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 262 */
int uninit_262(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 4455;
    p->f1 = 4458;
    p->f2 = 4461;
    p->f3 = 4464;
    int v;
    switch (p->f3) { case 3: v = p->f4; break; default: break; }
    free(p);
    return v;
}

/* CASE 263 */
int uninit_263(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 4472;
    p->f1 = 4475;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 264 */
int uninit_264(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 265 */
int uninit_265(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 4506;
    p->f1 = 4509;
    p->f2 = 4512;
    p->f3 = 4515;
    p->f4 = 4518;
    p->f5 = 4521;
    p->f6 = 4524;
    int v;
    if (p->f6) v = p->f7;
    free(p);
    return v;
}

/* CASE 266 */
int uninit_266(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 4523;
    p->f1 = 4526;
    p->f2 = 4529;
    p->f3 = 4532;
    p->f4 = 4535;
    p->f5 = 4538;
    int v;
    switch (p->f5) { case 2: v = p->f6; break; default: break; }
    free(p);
    return v;
}

/* CASE 267 */
int uninit_267(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 4540;
    p->f1 = 4543;
    p->f2 = 4546;
    p->f3 = 4549;
    p->f4 = 4552;
    int v;
    if (p->f4) { if (p->f4 != 0) v = p->f5; }
    free(p);
    return v;
}

/* CASE 268 */
int uninit_268(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 4557;
    p->f1 = 4560;
    p->f2 = 4563;
    p->f3 = 4566;
    int v;
    if (p->f3) v = p->f4;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 269 */
int uninit_269(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 4574;
    p->f1 = 4577;
    p->f2 = 4580;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 270 */
int uninit_270(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 4591;
    p->f1 = 4594;
    int v;
    switch (p->f1) { case 1: v = p->f2; break; default: break; }
    free(p);
    return v;
}

/* CASE 271 */
int uninit_271(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 4608;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 272 */
int uninit_272(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 273 */
int uninit_273(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 274 */
int uninit_274(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 4659;
    int v;
    switch (p->f0) { case 5: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 275 */
int uninit_275(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 4676;
    p->f1 = 4679;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 276 */
int uninit_276(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 4693;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 277 */
int uninit_277(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 4710;
    p->f1 = 4713;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 278 */
int uninit_278(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 4727;
    p->f1 = 4730;
    p->f2 = 4733;
    p->f3 = 4736;
    p->f4 = 4739;
    int v;
    switch (p->f4) { case 4: v = p->f5; break; default: break; }
    free(p);
    return v;
}

/* CASE 279 */
int uninit_279(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 4744;
    p->f1 = 4747;
    p->f2 = 4750;
    int v;
    if (p->f2) { if (p->f2 != 0) v = p->f3; }
    free(p);
    return v;
}

/* CASE 280 */
int uninit_280(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 4761;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 281 */
int uninit_281(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 4778;
    p->f1 = 4781;
    p->f2 = 4784;
    p->f3 = 4787;
    p->f4 = 4790;
    p->f5 = 4793;
    p->f6 = 4796;
    p->f7 = 4799;
    int v;
    if (p->f7) v = p->f8;
    free(p);
    return v;
}

/* CASE 282 */
int uninit_282(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 4795;
    p->f1 = 4798;
    p->f2 = 4801;
    p->f3 = 4804;
    p->f4 = 4807;
    p->f5 = 4810;
    p->f6 = 4813;
    int v;
    switch (p->f6) { case 3: v = p->f7; break; default: break; }
    free(p);
    return v;
}

/* CASE 283 */
int uninit_283(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 4812;
    p->f1 = 4815;
    p->f2 = 4818;
    p->f3 = 4821;
    p->f4 = 4824;
    p->f5 = 4827;
    int v;
    if (p->f5) { if (p->f5 != 0) v = p->f6; }
    free(p);
    return v;
}

/* CASE 284 */
int uninit_284(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 4829;
    p->f1 = 4832;
    p->f2 = 4835;
    p->f3 = 4838;
    p->f4 = 4841;
    int v;
    if (p->f4) v = p->f5;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 285 */
int uninit_285(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 4846;
    p->f1 = 4849;
    p->f2 = 4852;
    p->f3 = 4855;
    int v;
    if (p->f3) v = p->f4;
    free(p);
    return v;
}

/* CASE 286 */
int uninit_286(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 4863;
    p->f1 = 4866;
    p->f2 = 4869;
    int v;
    switch (p->f2) { case 2: v = p->f3; break; default: break; }
    free(p);
    return v;
}

/* CASE 287 */
int uninit_287(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 4880;
    p->f1 = 4883;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 288 */
int uninit_288(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 4897;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 289 */
int uninit_289(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 290 */
int uninit_290(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 1: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 291 */
int uninit_291(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 292 */
int uninit_292(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 4965;
    p->f1 = 4968;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 293 */
int uninit_293(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 4982;
    p->f1 = 4985;
    p->f2 = 4988;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 294 */
int uninit_294(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 5: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 295 */
int uninit_295(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 5016;
    p->f1 = 5019;
    p->f2 = 5022;
    p->f3 = 5025;
    int v;
    if (p->f3) { if (p->f3 != 0) v = p->f4; }
    free(p);
    return v;
}

/* CASE 296 */
int uninit_296(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 5033;
    p->f1 = 5036;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 297 */
int uninit_297(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 298 */
int uninit_298(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 5067;
    p->f1 = 5070;
    p->f2 = 5073;
    p->f3 = 5076;
    p->f4 = 5079;
    p->f5 = 5082;
    p->f6 = 5085;
    p->f7 = 5088;
    int v;
    switch (p->f7) { case 4: v = p->f8; break; default: break; }
    free(p);
    return v;
}

/* CASE 299 */
int uninit_299(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 5084;
    p->f1 = 5087;
    p->f2 = 5090;
    p->f3 = 5093;
    p->f4 = 5096;
    p->f5 = 5099;
    p->f6 = 5102;
    int v;
    if (p->f6) { if (p->f6 != 0) v = p->f7; }
    free(p);
    return v;
}

/* CASE 300 */
int uninit_300(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 5101;
    p->f1 = 5104;
    p->f2 = 5107;
    p->f3 = 5110;
    p->f4 = 5113;
    p->f5 = 5116;
    int v;
    if (p->f5) v = p->f6;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 301 */
int uninit_301(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 5118;
    p->f1 = 5121;
    p->f2 = 5124;
    p->f3 = 5127;
    p->f4 = 5130;
    int v;
    if (p->f4) v = p->f5;
    free(p);
    return v;
}

/* CASE 302 */
int uninit_302(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 5135;
    p->f1 = 5138;
    p->f2 = 5141;
    p->f3 = 5144;
    int v;
    switch (p->f3) { case 3: v = p->f4; break; default: break; }
    free(p);
    return v;
}

/* CASE 303 */
int uninit_303(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 5152;
    p->f1 = 5155;
    p->f2 = 5158;
    int v;
    if (p->f2) { if (p->f2 != 0) v = p->f3; }
    free(p);
    return v;
}

/* CASE 304 */
int uninit_304(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 5169;
    p->f1 = 5172;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 305 */
int uninit_305(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 306 */
int uninit_306(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 5203;
    int v;
    switch (p->f0) { case 2: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 307 */
int uninit_307(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 5220;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 308 */
int uninit_308(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 5237;
    p->f1 = 5240;
    p->f2 = 5243;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 309 */
int uninit_309(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 5254;
    p->f1 = 5257;
    p->f2 = 5260;
    p->f3 = 5263;
    int v;
    if (p->f3) v = p->f4;
    free(p);
    return v;
}

/* CASE 310 */
int uninit_310(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 5271;
    int v;
    switch (p->f0) { case 1: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 311 */
int uninit_311(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 5288;
    p->f1 = 5291;
    p->f2 = 5294;
    p->f3 = 5297;
    p->f4 = 5300;
    int v;
    if (p->f4) { if (p->f4 != 0) v = p->f5; }
    free(p);
    return v;
}

/* CASE 312 */
int uninit_312(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 5305;
    p->f1 = 5308;
    p->f2 = 5311;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 313 */
int uninit_313(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 5322;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 314 */
int uninit_314(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 5339;
    p->f1 = 5342;
    p->f2 = 5345;
    p->f3 = 5348;
    p->f4 = 5351;
    p->f5 = 5354;
    p->f6 = 5357;
    p->f7 = 5360;
    p->f8 = 5363;
    int v;
    switch (p->f8) { case 5: v = p->f9; break; default: break; }
    free(p);
    return v;
}

/* CASE 315 */
int uninit_315(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 5356;
    p->f1 = 5359;
    p->f2 = 5362;
    p->f3 = 5365;
    p->f4 = 5368;
    p->f5 = 5371;
    p->f6 = 5374;
    p->f7 = 5377;
    int v;
    if (p->f7) { if (p->f7 != 0) v = p->f8; }
    free(p);
    return v;
}

/* CASE 316 */
int uninit_316(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 5373;
    p->f1 = 5376;
    p->f2 = 5379;
    p->f3 = 5382;
    p->f4 = 5385;
    p->f5 = 5388;
    p->f6 = 5391;
    int v;
    if (p->f6) v = p->f7;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 317 */
int uninit_317(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 5390;
    p->f1 = 5393;
    p->f2 = 5396;
    p->f3 = 5399;
    p->f4 = 5402;
    p->f5 = 5405;
    int v;
    if (p->f5) v = p->f6;
    free(p);
    return v;
}

/* CASE 318 */
int uninit_318(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 5407;
    p->f1 = 5410;
    p->f2 = 5413;
    p->f3 = 5416;
    p->f4 = 5419;
    int v;
    switch (p->f4) { case 4: v = p->f5; break; default: break; }
    free(p);
    return v;
}

/* CASE 319 */
int uninit_319(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 5424;
    p->f1 = 5427;
    p->f2 = 5430;
    p->f3 = 5433;
    int v;
    if (p->f3) { if (p->f3 != 0) v = p->f4; }
    free(p);
    return v;
}

/* CASE 320 */
int uninit_320(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 5441;
    p->f1 = 5444;
    p->f2 = 5447;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 321 */
int uninit_321(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 322 */
int uninit_322(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 3: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 323 */
int uninit_323(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 5492;
    p->f1 = 5495;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 324 */
int uninit_324(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 325 */
int uninit_325(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 326 */
int uninit_326(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 5543;
    p->f1 = 5546;
    int v;
    switch (p->f1) { case 2: v = p->f2; break; default: break; }
    free(p);
    return v;
}

/* CASE 327 */
int uninit_327(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 5560;
    p->f1 = 5563;
    p->f2 = 5566;
    p->f3 = 5569;
    p->f4 = 5572;
    p->f5 = 5575;
    int v;
    if (p->f5) { if (p->f5 != 0) v = p->f6; }
    free(p);
    return v;
}

/* CASE 328 */
int uninit_328(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 5577;
    p->f1 = 5580;
    p->f2 = 5583;
    p->f3 = 5586;
    int v;
    if (p->f3) v = p->f4;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 329 */
int uninit_329(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 5594;
    p->f1 = 5597;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 330 */
int uninit_330(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 1: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 331 */
int uninit_331(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 5628;
    p->f1 = 5631;
    p->f2 = 5634;
    p->f3 = 5637;
    p->f4 = 5640;
    p->f5 = 5643;
    p->f6 = 5646;
    p->f7 = 5649;
    p->f8 = 5652;
    int v;
    if (p->f8) { if (p->f8 != 0) v = p->f9; }
    free(p);
    return v;
}

/* CASE 332 */
int uninit_332(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 5645;
    p->f1 = 5648;
    p->f2 = 5651;
    p->f3 = 5654;
    p->f4 = 5657;
    p->f5 = 5660;
    p->f6 = 5663;
    p->f7 = 5666;
    int v;
    if (p->f7) v = p->f8;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 333 */
int uninit_333(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 5662;
    p->f1 = 5665;
    p->f2 = 5668;
    p->f3 = 5671;
    p->f4 = 5674;
    p->f5 = 5677;
    p->f6 = 5680;
    int v;
    if (p->f6) v = p->f7;
    free(p);
    return v;
}

/* CASE 334 */
int uninit_334(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 5679;
    p->f1 = 5682;
    p->f2 = 5685;
    p->f3 = 5688;
    p->f4 = 5691;
    p->f5 = 5694;
    int v;
    switch (p->f5) { case 5: v = p->f6; break; default: break; }
    free(p);
    return v;
}

/* CASE 335 */
int uninit_335(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 5696;
    p->f1 = 5699;
    p->f2 = 5702;
    p->f3 = 5705;
    p->f4 = 5708;
    int v;
    if (p->f4) { if (p->f4 != 0) v = p->f5; }
    free(p);
    return v;
}

/* CASE 336 */
int uninit_336(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 5713;
    p->f1 = 5716;
    p->f2 = 5719;
    p->f3 = 5722;
    int v;
    if (p->f3) v = p->f4;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 337 */
int uninit_337(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 338 */
int uninit_338(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 5747;
    int v;
    switch (p->f0) { case 4: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 339 */
int uninit_339(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 340 */
int uninit_340(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 5781;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 341 */
int uninit_341(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 5798;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 342 */
int uninit_342(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 5815;
    p->f1 = 5818;
    p->f2 = 5821;
    int v;
    switch (p->f2) { case 3: v = p->f3; break; default: break; }
    free(p);
    return v;
}

/* CASE 343 */
int uninit_343(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 344 */
int uninit_344(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 5849;
    p->f1 = 5852;
    p->f2 = 5855;
    p->f3 = 5858;
    p->f4 = 5861;
    int v;
    if (p->f4) v = p->f5;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 345 */
int uninit_345(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 5866;
    p->f1 = 5869;
    p->f2 = 5872;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 346 */
int uninit_346(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 5883;
    int v;
    switch (p->f0) { case 2: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 347 */
int uninit_347(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 5900;
    p->f1 = 5903;
    p->f2 = 5906;
    p->f3 = 5909;
    p->f4 = 5912;
    p->f5 = 5915;
    p->f6 = 5918;
    p->f7 = 5921;
    p->f8 = 5924;
    p->f9 = 5927;
    int v;
    if (p->f9) { if (p->f9 != 0) v = p->f10; }
    free(p);
    return v;
}

/* CASE 348 */
int uninit_348(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 5917;
    p->f1 = 5920;
    p->f2 = 5923;
    p->f3 = 5926;
    p->f4 = 5929;
    p->f5 = 5932;
    p->f6 = 5935;
    p->f7 = 5938;
    p->f8 = 5941;
    int v;
    if (p->f8) v = p->f9;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 349 */
int uninit_349(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 5934;
    p->f1 = 5937;
    p->f2 = 5940;
    p->f3 = 5943;
    p->f4 = 5946;
    p->f5 = 5949;
    p->f6 = 5952;
    p->f7 = 5955;
    int v;
    if (p->f7) v = p->f8;
    free(p);
    return v;
}

/* CASE 350 */
int uninit_350(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 5951;
    p->f1 = 5954;
    p->f2 = 5957;
    p->f3 = 5960;
    p->f4 = 5963;
    p->f5 = 5966;
    p->f6 = 5969;
    int v;
    switch (p->f6) { case 1: v = p->f7; break; default: break; }
    free(p);
    return v;
}

/* CASE 351 */
int uninit_351(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 5968;
    p->f1 = 5971;
    p->f2 = 5974;
    p->f3 = 5977;
    p->f4 = 5980;
    p->f5 = 5983;
    int v;
    if (p->f5) { if (p->f5 != 0) v = p->f6; }
    free(p);
    return v;
}

/* CASE 352 */
int uninit_352(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 5985;
    p->f1 = 5988;
    p->f2 = 5991;
    p->f3 = 5994;
    p->f4 = 5997;
    int v;
    if (p->f4) v = p->f5;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 353 */
int uninit_353(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 354 */
int uninit_354(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 5: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 355 */
int uninit_355(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 6036;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 356 */
int uninit_356(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 6053;
    p->f1 = 6056;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 357 */
int uninit_357(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 6070;
    p->f1 = 6073;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 358 */
int uninit_358(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 6087;
    p->f1 = 6090;
    p->f2 = 6093;
    p->f3 = 6096;
    int v;
    switch (p->f3) { case 4: v = p->f4; break; default: break; }
    free(p);
    return v;
}

/* CASE 359 */
int uninit_359(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 6104;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 360 */
int uninit_360(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 6121;
    p->f1 = 6124;
    p->f2 = 6127;
    p->f3 = 6130;
    p->f4 = 6133;
    p->f5 = 6136;
    int v;
    if (p->f5) v = p->f6;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 361 */
int uninit_361(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 6138;
    p->f1 = 6141;
    p->f2 = 6144;
    p->f3 = 6147;
    int v;
    if (p->f3) v = p->f4;
    free(p);
    return v;
}

/* CASE 362 */
int uninit_362(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 6155;
    p->f1 = 6158;
    int v;
    switch (p->f1) { case 3: v = p->f2; break; default: break; }
    free(p);
    return v;
}

/* CASE 363 */
int uninit_363(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 364 */
int uninit_364(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 6189;
    p->f1 = 6192;
    p->f2 = 6195;
    p->f3 = 6198;
    p->f4 = 6201;
    p->f5 = 6204;
    p->f6 = 6207;
    p->f7 = 6210;
    p->f8 = 6213;
    p->f9 = 6216;
    int v;
    if (p->f9) v = p->f10;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 365 */
int uninit_365(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 6206;
    p->f1 = 6209;
    p->f2 = 6212;
    p->f3 = 6215;
    p->f4 = 6218;
    p->f5 = 6221;
    p->f6 = 6224;
    p->f7 = 6227;
    p->f8 = 6230;
    int v;
    if (p->f8) v = p->f9;
    free(p);
    return v;
}

/* CASE 366 */
int uninit_366(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 6223;
    p->f1 = 6226;
    p->f2 = 6229;
    p->f3 = 6232;
    p->f4 = 6235;
    p->f5 = 6238;
    p->f6 = 6241;
    p->f7 = 6244;
    int v;
    switch (p->f7) { case 2: v = p->f8; break; default: break; }
    free(p);
    return v;
}

/* CASE 367 */
int uninit_367(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 6240;
    p->f1 = 6243;
    p->f2 = 6246;
    p->f3 = 6249;
    p->f4 = 6252;
    p->f5 = 6255;
    p->f6 = 6258;
    int v;
    if (p->f6) { if (p->f6 != 0) v = p->f7; }
    free(p);
    return v;
}

/* CASE 368 */
int uninit_368(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 6257;
    p->f1 = 6260;
    p->f2 = 6263;
    p->f3 = 6266;
    p->f4 = 6269;
    p->f5 = 6272;
    int v;
    if (p->f5) v = p->f6;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 369 */
int uninit_369(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 370 */
int uninit_370(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 6291;
    int v;
    switch (p->f0) { case 1: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 371 */
int uninit_371(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 6308;
    p->f1 = 6311;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 372 */
int uninit_372(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 6325;
    p->f1 = 6328;
    p->f2 = 6331;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 373 */
int uninit_373(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 6342;
    p->f1 = 6345;
    p->f2 = 6348;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 374 */
int uninit_374(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 6359;
    p->f1 = 6362;
    p->f2 = 6365;
    p->f3 = 6368;
    p->f4 = 6371;
    int v;
    switch (p->f4) { case 5: v = p->f5; break; default: break; }
    free(p);
    return v;
}

/* CASE 375 */
int uninit_375(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 6376;
    p->f1 = 6379;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 376 */
int uninit_376(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 6393;
    p->f1 = 6396;
    p->f2 = 6399;
    p->f3 = 6402;
    p->f4 = 6405;
    p->f5 = 6408;
    p->f6 = 6411;
    int v;
    if (p->f6) v = p->f7;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 377 */
int uninit_377(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 6410;
    p->f1 = 6413;
    p->f2 = 6416;
    p->f3 = 6419;
    p->f4 = 6422;
    int v;
    if (p->f4) v = p->f5;
    free(p);
    return v;
}

/* CASE 378 */
int uninit_378(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 6427;
    p->f1 = 6430;
    p->f2 = 6433;
    int v;
    switch (p->f2) { case 4: v = p->f3; break; default: break; }
    free(p);
    return v;
}

/* CASE 379 */
int uninit_379(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 6444;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 380 */
int uninit_380(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 6461;
    p->f1 = 6464;
    p->f2 = 6467;
    p->f3 = 6470;
    p->f4 = 6473;
    p->f5 = 6476;
    p->f6 = 6479;
    p->f7 = 6482;
    p->f8 = 6485;
    p->f9 = 6488;
    p->f10 = 6491;
    int v;
    if (p->f10) v = p->f11;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 381 */
int uninit_381(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 6478;
    p->f1 = 6481;
    p->f2 = 6484;
    p->f3 = 6487;
    p->f4 = 6490;
    p->f5 = 6493;
    p->f6 = 6496;
    p->f7 = 6499;
    p->f8 = 6502;
    p->f9 = 6505;
    int v;
    if (p->f9) v = p->f10;
    free(p);
    return v;
}

/* CASE 382 */
int uninit_382(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 6495;
    p->f1 = 6498;
    p->f2 = 6501;
    p->f3 = 6504;
    p->f4 = 6507;
    p->f5 = 6510;
    p->f6 = 6513;
    p->f7 = 6516;
    p->f8 = 6519;
    int v;
    switch (p->f8) { case 3: v = p->f9; break; default: break; }
    free(p);
    return v;
}

/* CASE 383 */
int uninit_383(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 6512;
    p->f1 = 6515;
    p->f2 = 6518;
    p->f3 = 6521;
    p->f4 = 6524;
    p->f5 = 6527;
    p->f6 = 6530;
    p->f7 = 6533;
    int v;
    if (p->f7) { if (p->f7 != 0) v = p->f8; }
    free(p);
    return v;
}

/* CASE 384 */
int uninit_384(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 6529;
    p->f1 = 6532;
    p->f2 = 6535;
    p->f3 = 6538;
    p->f4 = 6541;
    p->f5 = 6544;
    p->f6 = 6547;
    int v;
    if (p->f6) v = p->f7;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 385 */
int uninit_385(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 386 */
int uninit_386(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 2: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 387 */
int uninit_387(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 388 */
int uninit_388(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 389 */
int uninit_389(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 6614;
    p->f1 = 6617;
    p->f2 = 6620;
    p->f3 = 6623;
    int v;
    if (p->f3) v = p->f4;
    free(p);
    return v;
}

/* CASE 390 */
int uninit_390(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 1: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 391 */
int uninit_391(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 6648;
    p->f1 = 6651;
    p->f2 = 6654;
    int v;
    if (p->f2) { if (p->f2 != 0) v = p->f3; }
    free(p);
    return v;
}

/* CASE 392 */
int uninit_392(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 393 */
int uninit_393(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 6682;
    p->f1 = 6685;
    p->f2 = 6688;
    p->f3 = 6691;
    p->f4 = 6694;
    p->f5 = 6697;
    int v;
    if (p->f5) v = p->f6;
    free(p);
    return v;
}

/* CASE 394 */
int uninit_394(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 6699;
    p->f1 = 6702;
    p->f2 = 6705;
    p->f3 = 6708;
    int v;
    switch (p->f3) { case 5: v = p->f4; break; default: break; }
    free(p);
    return v;
}

/* CASE 395 */
int uninit_395(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 6716;
    p->f1 = 6719;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 396 */
int uninit_396(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 397 */
int uninit_397(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 6750;
    p->f1 = 6753;
    p->f2 = 6756;
    p->f3 = 6759;
    p->f4 = 6762;
    p->f5 = 6765;
    p->f6 = 6768;
    p->f7 = 6771;
    p->f8 = 6774;
    p->f9 = 6777;
    p->f10 = 6780;
    int v;
    if (p->f10) v = p->f11;
    free(p);
    return v;
}

/* CASE 398 */
int uninit_398(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 6767;
    p->f1 = 6770;
    p->f2 = 6773;
    p->f3 = 6776;
    p->f4 = 6779;
    p->f5 = 6782;
    p->f6 = 6785;
    p->f7 = 6788;
    p->f8 = 6791;
    p->f9 = 6794;
    int v;
    switch (p->f9) { case 4: v = p->f10; break; default: break; }
    free(p);
    return v;
}

/* CASE 399 */
int uninit_399(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 6784;
    p->f1 = 6787;
    p->f2 = 6790;
    p->f3 = 6793;
    p->f4 = 6796;
    p->f5 = 6799;
    p->f6 = 6802;
    p->f7 = 6805;
    p->f8 = 6808;
    int v;
    if (p->f8) { if (p->f8 != 0) v = p->f9; }
    free(p);
    return v;
}

/* CASE 400 */
int uninit_400(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 6801;
    p->f1 = 6804;
    p->f2 = 6807;
    p->f3 = 6810;
    p->f4 = 6813;
    p->f5 = 6816;
    p->f6 = 6819;
    p->f7 = 6822;
    int v;
    if (p->f7) v = p->f8;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 401 */
int uninit_401(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 402 */
int uninit_402(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 6835;
    int v;
    switch (p->f0) { case 3: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 403 */
int uninit_403(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 6852;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 404 */
int uninit_404(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 6869;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 405 */
int uninit_405(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 406 */
int uninit_406(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 6903;
    int v;
    switch (p->f0) { case 2: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 407 */
int uninit_407(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 6920;
    p->f1 = 6923;
    p->f2 = 6926;
    p->f3 = 6929;
    int v;
    if (p->f3) { if (p->f3 != 0) v = p->f4; }
    free(p);
    return v;
}

/* CASE 408 */
int uninit_408(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 6937;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 409 */
int uninit_409(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 6954;
    p->f1 = 6957;
    p->f2 = 6960;
    p->f3 = 6963;
    p->f4 = 6966;
    p->f5 = 6969;
    p->f6 = 6972;
    int v;
    if (p->f6) v = p->f7;
    free(p);
    return v;
}

/* CASE 410 */
int uninit_410(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 6971;
    p->f1 = 6974;
    p->f2 = 6977;
    p->f3 = 6980;
    p->f4 = 6983;
    int v;
    switch (p->f4) { case 1: v = p->f5; break; default: break; }
    free(p);
    return v;
}

/* CASE 411 */
int uninit_411(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 6988;
    p->f1 = 6991;
    p->f2 = 6994;
    int v;
    if (p->f2) { if (p->f2 != 0) v = p->f3; }
    free(p);
    return v;
}

/* CASE 412 */
int uninit_412(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 7005;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 413 */
int uninit_413(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 7022;
    p->f1 = 7025;
    p->f2 = 7028;
    p->f3 = 7031;
    p->f4 = 7034;
    p->f5 = 7037;
    p->f6 = 7040;
    p->f7 = 7043;
    p->f8 = 7046;
    p->f9 = 7049;
    p->f10 = 7052;
    p->f11 = 7055;
    int v;
    if (p->f11) v = p->f12;
    free(p);
    return v;
}

/* CASE 414 */
int uninit_414(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 7039;
    p->f1 = 7042;
    p->f2 = 7045;
    p->f3 = 7048;
    p->f4 = 7051;
    p->f5 = 7054;
    p->f6 = 7057;
    p->f7 = 7060;
    p->f8 = 7063;
    p->f9 = 7066;
    p->f10 = 7069;
    int v;
    switch (p->f10) { case 5: v = p->f11; break; default: break; }
    free(p);
    return v;
}

/* CASE 415 */
int uninit_415(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 7056;
    p->f1 = 7059;
    p->f2 = 7062;
    p->f3 = 7065;
    p->f4 = 7068;
    p->f5 = 7071;
    p->f6 = 7074;
    p->f7 = 7077;
    p->f8 = 7080;
    p->f9 = 7083;
    int v;
    if (p->f9) { if (p->f9 != 0) v = p->f10; }
    free(p);
    return v;
}

/* CASE 416 */
int uninit_416(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 7073;
    p->f1 = 7076;
    p->f2 = 7079;
    p->f3 = 7082;
    p->f4 = 7085;
    p->f5 = 7088;
    p->f6 = 7091;
    p->f7 = 7094;
    p->f8 = 7097;
    int v;
    if (p->f8) v = p->f9;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 417 */
int uninit_417(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 418 */
int uninit_418(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 4: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 419 */
int uninit_419(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 7124;
    p->f1 = 7127;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 420 */
int uninit_420(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 7141;
    p->f1 = 7144;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 421 */
int uninit_421(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 7158;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 422 */
int uninit_422(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 7175;
    p->f1 = 7178;
    int v;
    switch (p->f1) { case 3: v = p->f2; break; default: break; }
    free(p);
    return v;
}

/* CASE 423 */
int uninit_423(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 7192;
    p->f1 = 7195;
    p->f2 = 7198;
    p->f3 = 7201;
    p->f4 = 7204;
    int v;
    if (p->f4) { if (p->f4 != 0) v = p->f5; }
    free(p);
    return v;
}

/* CASE 424 */
int uninit_424(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 7209;
    p->f1 = 7212;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 425 */
int uninit_425(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 7226;
    p->f1 = 7229;
    p->f2 = 7232;
    p->f3 = 7235;
    p->f4 = 7238;
    p->f5 = 7241;
    p->f6 = 7244;
    p->f7 = 7247;
    int v;
    if (p->f7) v = p->f8;
    free(p);
    return v;
}

/* CASE 426 */
int uninit_426(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 7243;
    p->f1 = 7246;
    p->f2 = 7249;
    p->f3 = 7252;
    p->f4 = 7255;
    p->f5 = 7258;
    int v;
    switch (p->f5) { case 2: v = p->f6; break; default: break; }
    free(p);
    return v;
}

/* CASE 427 */
int uninit_427(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 7260;
    p->f1 = 7263;
    p->f2 = 7266;
    p->f3 = 7269;
    int v;
    if (p->f3) { if (p->f3 != 0) v = p->f4; }
    free(p);
    return v;
}

/* CASE 428 */
int uninit_428(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 7277;
    p->f1 = 7280;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 429 */
int uninit_429(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 430 */
int uninit_430(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 7311;
    p->f1 = 7314;
    p->f2 = 7317;
    p->f3 = 7320;
    p->f4 = 7323;
    p->f5 = 7326;
    p->f6 = 7329;
    p->f7 = 7332;
    p->f8 = 7335;
    p->f9 = 7338;
    p->f10 = 7341;
    p->f11 = 7344;
    int v;
    switch (p->f11) { case 1: v = p->f12; break; default: break; }
    free(p);
    return v;
}

/* CASE 431 */
int uninit_431(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 7328;
    p->f1 = 7331;
    p->f2 = 7334;
    p->f3 = 7337;
    p->f4 = 7340;
    p->f5 = 7343;
    p->f6 = 7346;
    p->f7 = 7349;
    p->f8 = 7352;
    p->f9 = 7355;
    p->f10 = 7358;
    int v;
    if (p->f10) { if (p->f10 != 0) v = p->f11; }
    free(p);
    return v;
}

/* CASE 432 */
int uninit_432(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 7345;
    p->f1 = 7348;
    p->f2 = 7351;
    p->f3 = 7354;
    p->f4 = 7357;
    p->f5 = 7360;
    p->f6 = 7363;
    p->f7 = 7366;
    p->f8 = 7369;
    p->f9 = 7372;
    int v;
    if (p->f9) v = p->f10;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 433 */
int uninit_433(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 434 */
int uninit_434(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 7379;
    int v;
    switch (p->f0) { case 5: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 435 */
int uninit_435(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 436 */
int uninit_436(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 7413;
    p->f1 = 7416;
    p->f2 = 7419;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 437 */
int uninit_437(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 7430;
    p->f1 = 7433;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 438 */
int uninit_438(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 7447;
    p->f1 = 7450;
    p->f2 = 7453;
    int v;
    switch (p->f2) { case 4: v = p->f3; break; default: break; }
    free(p);
    return v;
}

/* CASE 439 */
int uninit_439(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 7464;
    p->f1 = 7467;
    p->f2 = 7470;
    p->f3 = 7473;
    p->f4 = 7476;
    p->f5 = 7479;
    int v;
    if (p->f5) { if (p->f5 != 0) v = p->f6; }
    free(p);
    return v;
}

/* CASE 440 */
int uninit_440(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 7481;
    p->f1 = 7484;
    p->f2 = 7487;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 441 */
int uninit_441(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 442 */
int uninit_442(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 7515;
    p->f1 = 7518;
    p->f2 = 7521;
    p->f3 = 7524;
    p->f4 = 7527;
    p->f5 = 7530;
    p->f6 = 7533;
    int v;
    switch (p->f6) { case 3: v = p->f7; break; default: break; }
    free(p);
    return v;
}

/* CASE 443 */
int uninit_443(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 7532;
    p->f1 = 7535;
    p->f2 = 7538;
    p->f3 = 7541;
    p->f4 = 7544;
    int v;
    if (p->f4) { if (p->f4 != 0) v = p->f5; }
    free(p);
    return v;
}

/* CASE 444 */
int uninit_444(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 7549;
    p->f1 = 7552;
    p->f2 = 7555;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 445 */
int uninit_445(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 7566;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 446 */
int uninit_446(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 7583;
    p->f1 = 7586;
    p->f2 = 7589;
    p->f3 = 7592;
    p->f4 = 7595;
    p->f5 = 7598;
    p->f6 = 7601;
    p->f7 = 7604;
    p->f8 = 7607;
    p->f9 = 7610;
    p->f10 = 7613;
    p->f11 = 7616;
    p->f12 = 7619;
    int v;
    switch (p->f12) { case 2: v = p->f13; break; default: break; }
    free(p);
    return v;
}

/* CASE 447 */
int uninit_447(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 7600;
    p->f1 = 7603;
    p->f2 = 7606;
    p->f3 = 7609;
    p->f4 = 7612;
    p->f5 = 7615;
    p->f6 = 7618;
    p->f7 = 7621;
    p->f8 = 7624;
    p->f9 = 7627;
    p->f10 = 7630;
    p->f11 = 7633;
    int v;
    if (p->f11) { if (p->f11 != 0) v = p->f12; }
    free(p);
    return v;
}

/* CASE 448 */
int uninit_448(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 7617;
    p->f1 = 7620;
    p->f2 = 7623;
    p->f3 = 7626;
    p->f4 = 7629;
    p->f5 = 7632;
    p->f6 = 7635;
    p->f7 = 7638;
    p->f8 = 7641;
    p->f9 = 7644;
    p->f10 = 7647;
    int v;
    if (p->f10) v = p->f11;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 449 */
int uninit_449(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 450 */
int uninit_450(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 1: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 451 */
int uninit_451(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 7668;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 452 */
int uninit_452(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 453 */
int uninit_453(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 7702;
    p->f1 = 7705;
    p->f2 = 7708;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 454 */
int uninit_454(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 7719;
    p->f1 = 7722;
    p->f2 = 7725;
    p->f3 = 7728;
    int v;
    switch (p->f3) { case 5: v = p->f4; break; default: break; }
    free(p);
    return v;
}

/* CASE 455 */
int uninit_455(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 456 */
int uninit_456(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 7753;
    p->f1 = 7756;
    p->f2 = 7759;
    p->f3 = 7762;
    int v;
    if (p->f3) v = p->f4;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 457 */
int uninit_457(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 7770;
    int v;
    if (p->f0) v = p->f1;
    free(p);
    return v;
}

/* CASE 458 */
int uninit_458(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 7787;
    p->f1 = 7790;
    p->f2 = 7793;
    p->f3 = 7796;
    p->f4 = 7799;
    p->f5 = 7802;
    p->f6 = 7805;
    p->f7 = 7808;
    int v;
    switch (p->f7) { case 4: v = p->f8; break; default: break; }
    free(p);
    return v;
}

/* CASE 459 */
int uninit_459(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 7804;
    p->f1 = 7807;
    p->f2 = 7810;
    p->f3 = 7813;
    p->f4 = 7816;
    p->f5 = 7819;
    int v;
    if (p->f5) { if (p->f5 != 0) v = p->f6; }
    free(p);
    return v;
}

/* CASE 460 */
int uninit_460(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 7821;
    p->f1 = 7824;
    p->f2 = 7827;
    p->f3 = 7830;
    int v;
    if (p->f3) v = p->f4;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 461 */
int uninit_461(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 7838;
    p->f1 = 7841;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 462 */
int uninit_462(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 3: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 463 */
int uninit_463(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 7872;
    p->f1 = 7875;
    p->f2 = 7878;
    p->f3 = 7881;
    p->f4 = 7884;
    p->f5 = 7887;
    p->f6 = 7890;
    p->f7 = 7893;
    p->f8 = 7896;
    p->f9 = 7899;
    p->f10 = 7902;
    p->f11 = 7905;
    p->f12 = 7908;
    int v;
    if (p->f12) { if (p->f12 != 0) v = p->f13; }
    free(p);
    return v;
}

/* CASE 464 */
int uninit_464(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 7889;
    p->f1 = 7892;
    p->f2 = 7895;
    p->f3 = 7898;
    p->f4 = 7901;
    p->f5 = 7904;
    p->f6 = 7907;
    p->f7 = 7910;
    p->f8 = 7913;
    p->f9 = 7916;
    p->f10 = 7919;
    p->f11 = 7922;
    int v;
    if (p->f11) v = p->f12;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 465 */
int uninit_465(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 466 */
int uninit_466(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 7923;
    int v;
    switch (p->f0) { case 2: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 467 */
int uninit_467(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 7940;
    p->f1 = 7943;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 468 */
int uninit_468(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 7957;
    int v;
    if (p->f0) v = p->f1;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 469 */
int uninit_469(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    p->f0 = 7974;
    p->f1 = 7977;
    p->f2 = 7980;
    p->f3 = 7983;
    int v;
    if (p->f3) v = p->f4;
    free(p);
    return v;
}

/* CASE 470 */
int uninit_470(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    p->f0 = 7991;
    p->f1 = 7994;
    p->f2 = 7997;
    p->f3 = 8000;
    p->f4 = 8003;
    int v;
    switch (p->f4) { case 1: v = p->f5; break; default: break; }
    free(p);
    return v;
}

/* CASE 471 */
int uninit_471(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 8008;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 472 */
int uninit_472(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 8025;
    p->f1 = 8028;
    p->f2 = 8031;
    p->f3 = 8034;
    p->f4 = 8037;
    int v;
    if (p->f4) v = p->f5;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 473 */
int uninit_473(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 8042;
    p->f1 = 8045;
    int v;
    if (p->f1) v = p->f2;
    free(p);
    return v;
}

/* CASE 474 */
int uninit_474(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    p->f0 = 8059;
    p->f1 = 8062;
    p->f2 = 8065;
    p->f3 = 8068;
    p->f4 = 8071;
    p->f5 = 8074;
    p->f6 = 8077;
    p->f7 = 8080;
    p->f8 = 8083;
    int v;
    switch (p->f8) { case 5: v = p->f9; break; default: break; }
    free(p);
    return v;
}

/* CASE 475 */
int uninit_475(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 8076;
    p->f1 = 8079;
    p->f2 = 8082;
    p->f3 = 8085;
    p->f4 = 8088;
    p->f5 = 8091;
    p->f6 = 8094;
    int v;
    if (p->f6) { if (p->f6 != 0) v = p->f7; }
    free(p);
    return v;
}

/* CASE 476 */
int uninit_476(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 8093;
    p->f1 = 8096;
    p->f2 = 8099;
    p->f3 = 8102;
    p->f4 = 8105;
    int v;
    if (p->f4) v = p->f5;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 477 */
int uninit_477(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 8110;
    p->f1 = 8113;
    p->f2 = 8116;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 478 */
int uninit_478(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 8127;
    int v;
    switch (p->f0) { case 4: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 479 */
int uninit_479(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    p->f0 = 8144;
    p->f1 = 8147;
    p->f2 = 8150;
    p->f3 = 8153;
    p->f4 = 8156;
    p->f5 = 8159;
    p->f6 = 8162;
    p->f7 = 8165;
    p->f8 = 8168;
    p->f9 = 8171;
    p->f10 = 8174;
    p->f11 = 8177;
    p->f12 = 8180;
    p->f13 = 8183;
    int v;
    if (p->f13) { if (p->f13 != 0) v = p->f14; }
    free(p);
    return v;
}

/* CASE 480 */
int uninit_480(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 8161;
    p->f1 = 8164;
    p->f2 = 8167;
    p->f3 = 8170;
    p->f4 = 8173;
    p->f5 = 8176;
    p->f6 = 8179;
    p->f7 = 8182;
    p->f8 = 8185;
    p->f9 = 8188;
    p->f10 = 8191;
    p->f11 = 8194;
    p->f12 = 8197;
    int v;
    if (p->f12) v = p->f13;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 481 */
int uninit_481(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 482 */
int uninit_482(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 3: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 483 */
int uninit_483(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 484 */
int uninit_484(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 8229;
    p->f1 = 8232;
    int v;
    if (p->f1) v = p->f2;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 485 */
int uninit_485(void) {
    U5 *p = (U5 *)malloc(sizeof(U5));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 486 */
int uninit_486(void) {
    U6 *p = (U6 *)malloc(sizeof(U6));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 2: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 487 */
int uninit_487(void) {
    U7 *p = (U7 *)malloc(sizeof(U7));
    if (p == NULL) return 0;
    p->f0 = 8280;
    p->f1 = 8283;
    int v;
    if (p->f1) { if (p->f1 != 0) v = p->f2; }
    free(p);
    return v;
}

/* CASE 488 */
int uninit_488(void) {
    U8 *p = (U8 *)malloc(sizeof(U8));
    if (p == NULL) return 0;
    p->f0 = 8297;
    p->f1 = 8300;
    p->f2 = 8303;
    p->f3 = 8306;
    p->f4 = 8309;
    p->f5 = 8312;
    int v;
    if (p->f5) v = p->f6;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 489 */
int uninit_489(void) {
    U9 *p = (U9 *)malloc(sizeof(U9));
    if (p == NULL) return 0;
    p->f0 = 8314;
    p->f1 = 8317;
    p->f2 = 8320;
    int v;
    if (p->f2) v = p->f3;
    free(p);
    return v;
}

/* CASE 490 */
int uninit_490(void) {
    U10 *p = (U10 *)malloc(sizeof(U10));
    if (p == NULL) return 0;
    int v;
    switch (p->f0) { case 1: v = p->f0; break; default: break; }
    free(p);
    return v;
}

/* CASE 491 */
int uninit_491(void) {
    U11 *p = (U11 *)malloc(sizeof(U11));
    if (p == NULL) return 0;
    p->f0 = 8348;
    p->f1 = 8351;
    p->f2 = 8354;
    p->f3 = 8357;
    p->f4 = 8360;
    p->f5 = 8363;
    p->f6 = 8366;
    p->f7 = 8369;
    int v;
    if (p->f7) { if (p->f7 != 0) v = p->f8; }
    free(p);
    return v;
}

/* CASE 492 */
int uninit_492(void) {
    U12 *p = (U12 *)malloc(sizeof(U12));
    if (p == NULL) return 0;
    p->f0 = 8365;
    p->f1 = 8368;
    p->f2 = 8371;
    p->f3 = 8374;
    p->f4 = 8377;
    p->f5 = 8380;
    int v;
    if (p->f5) v = p->f6;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 493 */
int uninit_493(void) {
    U13 *p = (U13 *)malloc(sizeof(U13));
    if (p == NULL) return 0;
    p->f0 = 8382;
    p->f1 = 8385;
    p->f2 = 8388;
    p->f3 = 8391;
    int v;
    if (p->f3) v = p->f4;
    free(p);
    return v;
}

/* CASE 494 */
int uninit_494(void) {
    U14 *p = (U14 *)malloc(sizeof(U14));
    if (p == NULL) return 0;
    p->f0 = 8399;
    p->f1 = 8402;
    int v;
    switch (p->f1) { case 5: v = p->f2; break; default: break; }
    free(p);
    return v;
}

/* CASE 495 */
int uninit_495(void) {
    U15 *p = (U15 *)malloc(sizeof(U15));
    if (p == NULL) return 0;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f0; }
    free(p);
    return v;
}

/* CASE 496 */
int uninit_496(void) {
    U16 *p = (U16 *)malloc(sizeof(U16));
    if (p == NULL) return 0;
    p->f0 = 8433;
    p->f1 = 8436;
    p->f2 = 8439;
    p->f3 = 8442;
    p->f4 = 8445;
    p->f5 = 8448;
    p->f6 = 8451;
    p->f7 = 8454;
    p->f8 = 8457;
    p->f9 = 8460;
    p->f10 = 8463;
    p->f11 = 8466;
    p->f12 = 8469;
    p->f13 = 8472;
    int v;
    if (p->f13) v = p->f14;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

/* CASE 497 */
int uninit_497(void) {
    U1 *p = (U1 *)malloc(sizeof(U1));
    if (p == NULL) return 0;
    int v;
    if (p->f0) v = p->f0;
    free(p);
    return v;
}

/* CASE 498 */
int uninit_498(void) {
    U2 *p = (U2 *)malloc(sizeof(U2));
    if (p == NULL) return 0;
    p->f0 = 8467;
    int v;
    switch (p->f0) { case 4: v = p->f1; break; default: break; }
    free(p);
    return v;
}

/* CASE 499 */
int uninit_499(void) {
    U3 *p = (U3 *)malloc(sizeof(U3));
    if (p == NULL) return 0;
    p->f0 = 8484;
    int v;
    if (p->f0) { if (p->f0 != 0) v = p->f1; }
    free(p);
    return v;
}

/* CASE 500 */
int uninit_500(void) {
    U4 *p = (U4 *)malloc(sizeof(U4));
    if (p == NULL) return 0;
    p->f0 = 8501;
    p->f1 = 8504;
    p->f2 = 8507;
    int v;
    if (p->f2) v = p->f3;
    g_uninit_sink = v;
    free(p);
    return g_uninit_sink;
}

#endif /* CPLUS_UNINIT_COMPILE_PROBE || CPLUS */
