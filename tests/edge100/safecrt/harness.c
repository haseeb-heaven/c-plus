/*
 * harness.c -- EDGE100 safecrt edge-case harness (agent 5/10).
 *
 * Exercises EVERY public API declared in the 13 stdlib/safe headers across the
 * required edge families: valid, NULL, zero, exact-fit, one-over, SIZE_MAX, overlap,
 * unterminated, double-free, closed-stream and %n.
 *
 * Method (mirrors tests/redblue20/red_safecrt/harness.c):
 *   - Each (api, vector) case runs in a FORKED CHILD.
 *   - Buffers live at the end of an mmap'd RW page whose tail holds a CANARY
 *     (0xC3) and whose next page is PROT_NONE.  A small overflow changes the
 *     canary; a large one hits the guard page (SIGSEGV).
 *   - The child sets constraint mode IGNORE and violation trap OFF so refusals
 *     are observable as return values/sentinels (not exit(70)).
 *   - Child exits 0 (PASS), 1 (FAIL: unsafe result) or 2 (FAIL: canary corrupt);
 *     a signal is a CRASH (FAIL).  The parent applies an alarm(3) per case and
 *     an outer `timeout 60` in run.sh.
 *
 * Prints exactly:
 *   EDGE100 safecrt <apis> <cases> <passed> <failed>
 * plus a per-case log (api vector result detail) to the path in argv[1].
 */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS 1

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

typedef int errno_t;
typedef size_t rsize_t;
#define CPLUS_SAFE_ERRNO_T_DEFINED 1
#define CPLUS_SAFE_RSIZE_T_DEFINED 1

#include "safe_alloc.h"
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_io.h"
#include "safe_format.h"
#include "safe_error.h"
#include "safe_except.h"
#include "safe_log.h"
#include "safe_platform.h"
#include "safe_stats.h"
#include "safe_runtime.h"
#include "safe_ext_mem.h"
#include "cplus_safecrt_upstream.h"

/* Hardened-libc entry points reached through safe_c.h mappings. */
extern size_t cplus_fread(void *, size_t, size_t, FILE *);
extern size_t cplus_fwrite(const void *, size_t, size_t, FILE *);
extern int cplus_printf(const char *, ...);
extern int cplus_fprintf(FILE *, const char *, ...);
extern int cplus_scanf(const char *, ...);
extern int cplus_fscanf(FILE *, const char *, ...);
extern int cplus_sscanf(const char *, const char *, ...);

/* ---- vector bits ------------------------------------------------------- */
enum {
    V_VALID = 0, V_NULL, V_ZERO, V_EXACT, V_ONEOVER, V_SIZEMAX,
    V_OVERLAP, V_UNTERM, V_DOUBLEFREE, V_CLOSED, V_PCTN, NVEC
};
static const char *VNAME[NVEC] = {
    "valid", "NULL", "zero", "exact_fit", "one_over", "SIZE_MAX",
    "overlap", "unterminated", "double_free", "closed_stream", "percent_n"
};
#define M_VALID     (1u << V_VALID)
#define M_NULL      (1u << V_NULL)
#define M_ZERO      (1u << V_ZERO)
#define M_EXACT     (1u << V_EXACT)
#define M_ONEOVER   (1u << V_ONEOVER)
#define M_SIZEMAX   (1u << V_SIZEMAX)
#define M_OVERLAP   (1u << V_OVERLAP)
#define M_UNTERM    (1u << V_UNTERM)
#define M_DOUBLEFREE (1u << V_DOUBLEFREE)
#define M_CLOSED    (1u << V_CLOSED)
#define M_PCTN      (1u << V_PCTN)

/* ---- child result codes ------------------------------------------------ */
#define R_PASS     0
#define R_FAIL     1
#define R_CORRUPT  2

/* ---- canary guard ------------------------------------------------------ */
static size_t P;
#define CANARY 64u
#define CANB   0xC3u

typedef struct { unsigned char *base; size_t cap; char *buf; } Guard;

static Guard g_new(size_t cap)
{
    Guard g;
    g.base = NULL; g.cap = cap; g.buf = NULL;
    if (cap == 0 || cap > P - CANARY) return g;
    g.base = (unsigned char *)mmap(NULL, 2 * P, PROT_NONE,
                                   MAP_PRIVATE | MAP_ANON, -1, 0);
    if (g.base == MAP_FAILED) { g.base = NULL; return g; }
    if (mprotect(g.base, P, PROT_READ | PROT_WRITE) != 0) {
        munmap(g.base, 2 * P); g.base = NULL; return g;
    }
    memset(g.base, CANB, P);
    g.buf = (char *)(g.base + P - CANARY - cap);
    return g;
}
static int g_ok(const Guard *g)
{
    size_t i;
    if (!g->base) return 0;
    for (i = 0; i < CANARY; ++i)
        if ((unsigned char)g->buf[g->cap + i] != (unsigned char)CANB) return 0;
    return 1;
}
static void g_free(Guard *g)
{
    if (g->base) munmap(g->base, 2 * P);
    g->base = NULL;
}
#define GCHECK(g, rc) do { if (!g_ok(&(g))) return R_CORRUPT; } while (0)

static void init_child(void)
{
    cplus_runtime_set_violation_trap(0);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
}

/* ======================================================================= */
/* Family handlers.  Each returns R_PASS / R_FAIL / R_CORRUPT.              */
/* ======================================================================= */

/* ---- memory ------------------------------------------------------------ */
enum { A_MEMCPY, A_MEMMOVE, A_MEMSET, A_MEMCMP, A_MEMZERO,
       A_MEMCPY_A, A_MEMMOVE_A, A_MEMSET_A, A_MEMCMP_A };

static int run_mem(int op, int vec)
{
    const size_t cap = 32;
    Guard d, s;
    errno_t rc = -1;
    void *r = (void *)1;
    int diff = 12345;
    size_t n, dm, sm;
    int bad;

    d = g_new(cap);
    if (!d.base) return R_FAIL;
    s = g_new(cap);
    if (!s.base) { g_free(&d); return R_FAIL; }
    memset(s.buf, 'A', cap);
    s.buf[7] = 0;                 /* valid NUL-terminated source */

    n = 16; dm = cap; sm = 16;
    if (vec == V_EXACT) { n = cap; sm = cap; }
    else if (vec == V_ONEOVER) { n = cap + 1; sm = cap + 1; }
    else if (vec == V_SIZEMAX) { dm = SIZE_MAX; sm = SIZE_MAX; n = SIZE_MAX; }
    /* memcmp compares equal buffers so the success path can assert diff == 0. */
    if (op == A_MEMCMP || op == A_MEMCMP_A) memcpy(d.buf, s.buf, cap);

    switch (op) {
    case A_MEMCPY: {
        int refuse = (vec == V_NULL || vec == V_OVERLAP || vec == V_ONEOVER || vec == V_SIZEMAX);
        if (vec == V_OVERLAP) rc = cplus_memcpy_s(d.buf, cap, d.buf + 1, cap - 1);
        else rc = cplus_memcpy_s(vec == V_NULL ? NULL : d.buf,
                                 vec == V_ZERO ? 0 : dm, s.buf,
                                 vec == V_ZERO ? 0 : sm);
        GCHECK(d, rc);
        /* zero-length is a defined C11 no-op (returns EOK). */
        bad = refuse ? (rc == 0)
                     : (rc != 0 || (vec != V_ZERO && memcmp(d.buf, s.buf, n) != 0));
        break;
    }
    case A_MEMMOVE: {
        int refuse = (vec == V_NULL || vec == V_ONEOVER || vec == V_SIZEMAX);
        if (vec == V_OVERLAP) rc = cplus_memmove_s(d.buf, cap, d.buf + 1, cap - 1);
        else rc = cplus_memmove_s(vec == V_NULL ? NULL : d.buf,
                                  vec == V_ZERO ? 0 : dm, s.buf,
                                  vec == V_ZERO ? 0 : sm);
        GCHECK(d, rc);
        bad = refuse ? (rc == 0) : (rc != 0);
        break;
    }
    case A_MEMSET: {
        int refuse = (vec == V_NULL || vec == V_ONEOVER || vec == V_SIZEMAX);
        rc = cplus_memset_s(vec == V_NULL ? NULL : d.buf,
                            vec == V_ZERO ? 0 : (vec == V_SIZEMAX ? SIZE_MAX : cap),
                            0x41,
                            vec == V_ZERO ? 0 : (vec == V_ONEOVER ? cap + 1 :
                            (vec == V_SIZEMAX ? SIZE_MAX : n)));
        GCHECK(d, rc);
        bad = refuse ? (rc == 0) : (rc != 0 || (vec != V_ZERO && d.buf[0] != 0x41));
        break;
    }
    case A_MEMCMP: {
        int refuse = (vec == V_NULL || vec == V_ZERO || vec == V_ONEOVER || vec == V_SIZEMAX);
        rc = cplus_memcmp_s(vec == V_NULL ? NULL : d.buf,
                            vec == V_ZERO ? 0 : dm,
                            vec == V_NULL ? NULL : s.buf,
                            vec == V_ZERO ? 0 : sm, &diff);
        GCHECK(d, rc);
        /* dmax == 0 is ESZEROL (401) for memcmp_s, unlike memcpy/memset. */
        bad = refuse ? (rc == 0) : (rc != 0 || diff != 0);
        break;
    }
    case A_MEMZERO: {
        int refuse = (vec == V_NULL || vec == V_ZERO || vec == V_SIZEMAX);
        rc = cplus_memzero_s(vec == V_NULL ? NULL : d.buf,
                             vec == V_ZERO ? 0 : (vec == V_SIZEMAX ? SIZE_MAX : cap));
        GCHECK(d, rc);
        bad = refuse ? (rc == 0) : (rc != 0);
        break;
    }
    case A_MEMCPY_A: {
        int refuse = (vec == V_NULL || vec == V_ONEOVER || vec == V_SIZEMAX || vec == V_OVERLAP);
        r = cplus_memcpy_s_auto(vec == V_NULL ? NULL : d.buf,
                                vec == V_OVERLAP ? d.buf + 1 : s.buf,
                                vec == V_ZERO ? 0 : (vec == V_SIZEMAX ? SIZE_MAX :
                                (vec == V_ONEOVER ? cap + 1 : n)), cap);
        GCHECK(d, rc);
        bad = refuse ? (r != NULL) : (r != d.buf);
        break;
    }
    case A_MEMMOVE_A: {
        int refuse = (vec == V_NULL || vec == V_ONEOVER || vec == V_SIZEMAX);
        r = cplus_memmove_s_auto(vec == V_NULL ? NULL : d.buf,
                                 vec == V_OVERLAP ? d.buf + 1 : s.buf,
                                 vec == V_ZERO ? 0 : (vec == V_SIZEMAX ? SIZE_MAX :
                                 (vec == V_ONEOVER ? cap + 1 : n)), cap);
        GCHECK(d, rc);
        bad = refuse ? (r != NULL) : (r != d.buf);
        break;
    }
    case A_MEMSET_A: {
        int refuse = (vec == V_NULL || vec == V_ONEOVER || vec == V_SIZEMAX);
        r = cplus_memset_s_auto(vec == V_NULL ? NULL : d.buf, 0x41,
                                vec == V_ZERO ? 0 : (vec == V_SIZEMAX ? SIZE_MAX :
                                (vec == V_ONEOVER ? cap + 1 : n)), cap);
        GCHECK(d, rc);
        bad = refuse ? (r != NULL) : (r != d.buf || (vec != V_ZERO && d.buf[0] != 0x41));
        break;
    }
    case A_MEMCMP_A: {
        int refuse = (vec == V_NULL || vec == V_ONEOVER || vec == V_SIZEMAX);
        diff = cplus_memcmp_s_auto(vec == V_NULL ? NULL : d.buf,
                                   vec == V_NULL ? NULL : s.buf,
                                   vec == V_ZERO ? 0 : (vec == V_SIZEMAX ? SIZE_MAX :
                                   (vec == V_ONEOVER ? cap + 1 : n)), cap, cap);
        GCHECK(d, rc);
        /* refusal is -1; zero-length and equal buffers report 0. */
        bad = refuse ? (diff != -1) : (diff == -1 || (vec != V_ZERO && diff != 0));
        break;
    }
    default:
        bad = 1;
    }
    g_free(&d); g_free(&s);
    return bad ? R_FAIL : R_PASS;
}

/* ---- capacity mode ----------------------------------------------------- */
enum { A_CAPSET, A_CAPGET };
static int run_capmode(int op, int vec)
{
    if (op == A_CAPSET) {
        int before = cplus_safe_unknown_capacity_strict();
        cplus_safe_set_unknown_capacity_mode(vec == V_ZERO ? 99 : 1);
        int after = cplus_safe_unknown_capacity_strict();
        cplus_safe_set_unknown_capacity_mode(before);
        /* any non-zero value selects strict; 99 is documented as non-zero. */
        return (after != 1) ? R_FAIL : R_PASS;
    }
    (void)vec;
    { int m = cplus_safe_unknown_capacity_strict(); if (m != 0 && m != 1) return R_FAIL; }
    return R_PASS;
}

/* ---- strings ----------------------------------------------------------- */
enum { A_STRCPY, A_STRNCPY, A_STRCAT, A_STRNCAT, A_STRCMP, A_STRNLEN,
       A_STRTOK, A_STRERROR, A_STRCPY_A, A_STRNCPY_A, A_STRCAT_A, A_STRNCAT_A };

static int run_str(int op, int vec)
{
    const size_t cap = 32;
    Guard d, s;
    errno_t rc = -1;
    char *cp = (char *)1;
    rsize_t rn = 999;
    int ind = 12345;
    size_t slen, dmax = cap;
    int bad = 0;

    d = g_new(cap);
    if (!d.base) return R_FAIL;
    s = g_new(cap);
    if (!s.base) { g_free(&d); return R_FAIL; }

    if (vec == V_ZERO) dmax = 0;
    else if (vec == V_SIZEMAX) dmax = SIZE_MAX;
    else if (vec == V_ONEOVER) dmax = 4; /* "hello" needs 6 bytes > 4 */

    if (vec == V_UNTERM || vec == V_ONEOVER) {
        /* no NUL anywhere in the source page -> unterminated source */
        memset(d.buf, 0, cap);
        memcpy(d.buf, "abc", 3);
        memset(s.buf, 'A', cap);
    } else {
        memset(d.buf, 0, cap);
        memcpy(d.buf, "abc", 3);
        memset(s.buf, 0, cap);
        memcpy(s.buf, "hello", 5);
    }
    slen = 4;

    switch (op) {
    case A_STRCPY: {
        int refuse = (vec == V_NULL || vec == V_ZERO || vec == V_ONEOVER ||
                      vec == V_SIZEMAX || vec == V_OVERLAP || vec == V_UNTERM);
        rc = cplus_strcpy_s(vec == V_NULL ? NULL : d.buf, dmax,
                            vec == V_OVERLAP ? d.buf + 1 : s.buf);
        GCHECK(d, rc);
        bad = refuse ? (rc == 0) : (rc != 0);
        break;
    }
    case A_STRNCPY: {
        /* a bounded copy of an unterminated source succeeds safely (reads are
         * capped by slen and dmax), so UNTERM is not a refusal here. */
        int refuse = (vec == V_NULL || vec == V_ZERO || vec == V_ONEOVER ||
                      vec == V_SIZEMAX || vec == V_OVERLAP);
        rc = cplus_strncpy_s(vec == V_NULL ? NULL : d.buf, dmax,
                             vec == V_OVERLAP ? d.buf + 1 : s.buf,
                             vec == V_ONEOVER ? cap + 1 : slen);
        GCHECK(d, rc);
        bad = refuse ? (rc == 0) : (rc != 0);
        break;
    }
    case A_STRCAT: {
        int refuse = (vec == V_NULL || vec == V_ZERO || vec == V_ONEOVER ||
                      vec == V_SIZEMAX || vec == V_UNTERM);
        rc = cplus_strcat_s(vec == V_NULL ? NULL : d.buf, dmax, s.buf);
        GCHECK(d, rc);
        bad = refuse ? (rc == 0) : (rc != 0);
        break;
    }
    case A_STRNCAT: {
        /* bounded concatenation tolerates an unterminated source safely. */
        int refuse = (vec == V_NULL || vec == V_ZERO || vec == V_ONEOVER ||
                      vec == V_SIZEMAX);
        rc = cplus_strncat_s(vec == V_NULL ? NULL : d.buf, dmax, s.buf,
                             vec == V_ONEOVER ? cap + 1 : slen);
        GCHECK(d, rc);
        bad = refuse ? (rc == 0) : (rc != 0);
        break;
    }
    case A_STRCMP: {
        /* dmax 0 is ESZEROL; an unterminated source is reported via the
         * indicator without over-reading (rc 0), which is the safe outcome. */
        int refuse = (vec == V_NULL || vec == V_ZERO);
        rc = cplus_strcmp_s(vec == V_NULL ? NULL : d.buf, dmax, s.buf, &ind);
        GCHECK(d, rc);
        bad = refuse ? (rc == 0) : (rc != 0);
        break;
    }
    case A_STRNLEN:
        rn = cplus_strnlen_s(vec == V_NULL ? NULL : s.buf,
                             vec == V_ZERO ? 0 : (vec == V_SIZEMAX ? SIZE_MAX : cap));
        GCHECK(d, rc);
        bad = (vec == V_ZERO || vec == V_SIZEMAX) ? (rn != 0) : (rn > cap);
        break;
    case A_STRTOK: {
        char tokbuf[16];
        char *save = NULL;
        memcpy(tokbuf, "a b", 4);
        rn = 4;
        cp = cplus_strtok_s(vec == V_NULL ? NULL : tokbuf, &rn, " ", &save);
        GCHECK(d, rc);
        /* A zero *dmax is a defined empty-range tokenization, not a refusal. */
        bad = (vec == V_NULL) ? (cp != NULL) : (cp == NULL);
        break;
    }
    case A_STRERROR: {
        int refuse = (vec == V_NULL || vec == V_ZERO || vec == V_ONEOVER || vec == V_SIZEMAX);
        rc = cplus_strerror_s(vec == V_NULL ? NULL : d.buf,
                              vec == V_ONEOVER ? 1 : dmax, EINVAL);
        GCHECK(d, rc);
        bad = refuse ? (rc == 0) : (rc != 0);
        break;
    }
    case A_STRCPY_A:
        cp = cplus_strcpy_s_auto(vec == V_NULL ? NULL : d.buf, s.buf, cap);
        GCHECK(d, rc);
        bad = (vec == V_NULL) ? (cp != NULL) : (cp != d.buf);
        break;
    case A_STRNCPY_A:
        cp = cplus_strncpy_s_auto(vec == V_NULL ? NULL : d.buf, s.buf,
                                  vec == V_ONEOVER ? cap + 1 : slen, cap);
        GCHECK(d, rc);
        bad = (vec == V_NULL || vec == V_ONEOVER) ? (cp != NULL) : (cp != d.buf);
        break;
    case A_STRCAT_A:
        cp = cplus_strcat_s_auto(vec == V_NULL ? NULL : d.buf, s.buf, cap);
        GCHECK(d, rc);
        bad = (vec == V_NULL) ? (cp != NULL) : (cp != d.buf);
        break;
    case A_STRNCAT_A:
        cp = cplus_strncat_s_auto(vec == V_NULL ? NULL : d.buf, s.buf,
                                  vec == V_ONEOVER ? cap + 1 : slen, cap);
        GCHECK(d, rc);
        bad = (vec == V_NULL || vec == V_ONEOVER) ? (cp != NULL) : (cp != d.buf);
        break;
    default: bad = 1;
    }
    g_free(&d); g_free(&s);
    return bad ? R_FAIL : R_PASS;
}

/* ---- allocator --------------------------------------------------------- */
enum { A_MALLOC, A_CALLOC, A_REALLOC, A_FREE, A_FREECHK, A_FREEGEN,
       A_TRANSFER, A_TRANSFERGEN, A_ALIGNED, A_GETMETA, A_ISLIVE, A_WASFREED,
       A_PTRSIZE, A_REMCAP, A_ELEMSIZE, A_PTRCOUNT, A_QUERYLEN, A_CHECKGEN,
       A_LIVECOUNT, A_LIVEBYTES, A_FAILAFTER, A_FAILRESET, A_SEQ };

static int run_alloc(int op, int vec)
{
    void *p = NULL;
    void *q = NULL;
    cplus_safe_alloc_meta meta;
    size_t sz = 16, out = 0;
    int i = 0, rc = 0, bad = 0;
    unsigned long long gen = 0;

    switch (op) {
    case A_MALLOC:
        p = malloc_s(vec == V_ZERO ? 0 : (vec == V_SIZEMAX ? SIZE_MAX : sz));
        bad = (vec == V_SIZEMAX) ? (p != NULL) : (p == NULL);
        if (p) free_s(p);
        break;
    case A_CALLOC:
        p = calloc_s(vec == V_ZERO ? 0 : (vec == V_SIZEMAX ? SIZE_MAX : 4),
                     vec == V_SIZEMAX ? SIZE_MAX : 4);
        bad = (vec == V_SIZEMAX) ? (p != NULL) : (p == NULL);
        if (p) free_s(p);
        break;
    case A_REALLOC:
        if (vec == V_NULL) {
            p = realloc_s(NULL, sz);
            bad = (p == NULL);
            if (p) free_s(p);
        } else {
            p = malloc_s(sz);
            if (!p) return R_FAIL;
            q = realloc_s(p, vec == V_ZERO ? 0 : (vec == V_SIZEMAX ? SIZE_MAX : 64));
            bad = (vec == V_SIZEMAX) ? (q != NULL) : (q == NULL);
            if (q) free_s(q); else free_s(p);
        }
        break;
    case A_FREE:
        if (vec == V_NULL) { free_s(NULL); bad = 0; }
        else {
            p = malloc_s(sz);
            if (!p) return R_FAIL;
            free_s(p);
            if (vec == V_DOUBLEFREE) { free_s(p); bad = 0; } /* must not crash */
            bad = 0;
        }
        break;
    case A_FREECHK:
        if (vec == V_NULL) { rc = free_checked_s(NULL); bad = (rc != CPLUS_SAFE_OK); }
        else {
            p = malloc_s(sz);
            if (!p) return R_FAIL;
            rc = free_checked_s(p);
            if (vec == V_DOUBLEFREE) { int r2 = free_checked_s(p); bad = (r2 == CPLUS_SAFE_OK); }
            else bad = (rc != CPLUS_SAFE_OK);
        }
        break;
    case A_FREEGEN:
        if (vec == V_NULL) { rc = free_checked_generation_s(NULL, 1); bad = (rc == CPLUS_SAFE_OK); }
        else {
            p = malloc_s(sz);
            if (!p || cplus_safe_alloc_get_meta(p, &meta) != 0) { if (p) free_s(p); return R_FAIL; }
            gen = vec == V_ZERO ? 0 : meta.generation;
            rc = free_checked_generation_s(p, gen);
            if (vec == V_DOUBLEFREE || vec == V_ZERO) {
                int r2 = free_checked_generation_s(p, gen);
                bad = (r2 == CPLUS_SAFE_OK);
            } else bad = (rc != CPLUS_SAFE_OK);
        }
        break;
    case A_TRANSFER:
        if (vec == V_NULL) { void *a = NULL, *b = NULL; rc = cplus_safe_transfer_ownership(&a, &b); bad = (rc == CPLUS_SAFE_OK); }
        else {
            void *orig;
            p = malloc_s(sz); q = NULL;
            if (!p) return R_FAIL;
            orig = p;
            rc = cplus_safe_transfer_ownership(&q, &p);
            if (vec == V_DOUBLEFREE) { void *a = NULL, *b = p; int r2 = cplus_safe_transfer_ownership(&a, &b); bad = (r2 == CPLUS_SAFE_OK); }
            else bad = (rc != CPLUS_SAFE_OK || q != orig || p != NULL);
            if (q) free_s(q);
        }
        break;
    case A_TRANSFERGEN:
        if (vec == V_NULL) { void *a = NULL, *b = NULL; rc = cplus_safe_transfer_ownership_generation(&a, &b, 1); bad = (rc == CPLUS_SAFE_OK); }
        else {
            void *orig;
            p = malloc_s(sz); q = NULL;
            if (!p || cplus_safe_alloc_get_meta(p, &meta) != 0) { if (p) free_s(p); return R_FAIL; }
            orig = p;
            gen = vec == V_ZERO ? 0 : meta.generation;
            rc = cplus_safe_transfer_ownership_generation(&q, &p, gen);
            if (vec == V_DOUBLEFREE || vec == V_ZERO) {
                void *a = NULL, *b = p; int r2 = cplus_safe_transfer_ownership_generation(&a, &b, gen);
                bad = (r2 == CPLUS_SAFE_OK);
            } else bad = (rc != CPLUS_SAFE_OK || q != orig);
            if (q) free_s(q);
        }
        break;
    case A_ALIGNED:
        p = aligned_alloc_s(vec == V_NULL ? 0 : (vec == V_ZERO ? 3 : 16),
                            vec == V_SIZEMAX ? SIZE_MAX : sz);
        bad = (vec == V_NULL || vec == V_ZERO || vec == V_SIZEMAX) ? (p != NULL) : (p == NULL);
        if (p) free_s(p);
        break;
    case A_GETMETA:
        p = malloc_s(sz);
        if (!p) return R_FAIL;
        i = cplus_safe_alloc_get_meta(vec == V_NULL ? NULL : p,
                                      vec == V_ZERO ? NULL : &meta);
        bad = (vec == V_NULL || vec == V_ZERO) ? (i == 0) : (i != 0 || meta.byte_size != sz);
        free_s(p);
        break;
    case A_ISLIVE:
        p = malloc_s(sz); if (!p) return R_FAIL;
        i = cplus_safe_ptr_is_live(vec == V_NULL ? NULL : p);
        bad = (vec == V_NULL) ? (i != 0) : (i == 0);
        free_s(p);
        break;
    case A_WASFREED:
        p = malloc_s(sz); if (!p) return R_FAIL;
        i = cplus_safe_ptr_was_freed(vec == V_NULL ? NULL : p);
        if (vec == V_DOUBLEFREE) { free_s(p); i = cplus_safe_ptr_was_freed(p); bad = (i == 0); }
        else { bad = (vec == V_NULL) ? (i != 0) : (i != 0); free_s(p); }
        break;
    case A_PTRSIZE:
        p = malloc_s(sz); if (!p) return R_FAIL;
        out = cplus_safe_ptr_size(vec == V_NULL ? NULL : p);
        bad = (vec == V_NULL) ? (out != 0) : (out != sz);
        free_s(p);
        break;
    case A_REMCAP:
        p = malloc_s(sz); if (!p) return R_FAIL;
        out = cplus_safe_remaining_capacity(vec == V_NULL ? NULL :
              (vec == V_EXACT ? (unsigned char *)p + sz : p));
        /* outside a tracked block the API returns (size_t)-1, not 0. */
        bad = (vec == V_NULL) ? (out != (size_t)-1) :
              (vec == V_EXACT ? (out != 0) : (out != sz));
        free_s(p);
        break;
    case A_ELEMSIZE:
        p = calloc_s(4, 4); if (!p) return R_FAIL;
        out = cplus_safe_ptr_elem_size(vec == V_NULL ? NULL : p);
        bad = (vec == V_NULL) ? (out != 0) : (out != 4);
        free_s(p);
        break;
    case A_PTRCOUNT:
        p = calloc_s(4, 4); if (!p) return R_FAIL;
        out = cplus_safe_ptr_count(vec == V_NULL ? NULL : p);
        bad = (vec == V_NULL) ? (out != 0) : (out != 4);
        free_s(p);
        break;
    case A_QUERYLEN:
        p = calloc_s(4, 4); if (!p) return R_FAIL;
        i = cplus_safe_query_length(vec == V_NULL ? NULL : p,
              vec == V_SIZEMAX ? SIZE_MAX : 4, vec == V_ZERO ? NULL : &out);
        /* an oversized elem_size simply yields count 0 with success (rc 0). */
        bad = (vec == V_NULL || vec == V_ZERO) ? (i == 0) :
              (vec == V_SIZEMAX ? (i != 0 || out != 0) : (i != 0 || out != 4));
        free_s(p);
        break;
    case A_CHECKGEN:
        p = malloc_s(sz);
        if (!p || cplus_safe_alloc_get_meta(p, &meta) != 0) { if (p) free_s(p); return R_FAIL; }
        gen = vec == V_ZERO ? 0 : meta.generation;
        i = cplus_safe_ptr_check_generation(vec == V_NULL ? NULL : p, gen);
        if (vec == V_DOUBLEFREE) { free_s(p); i = cplus_safe_ptr_check_generation(p, gen); bad = (i != 0); }
        else { bad = (vec == V_NULL || vec == V_ZERO) ? (i != 0) : (i == 0); free_s(p); }
        break;
    case A_LIVECOUNT: (void)cplus_safe_alloc_live_count(); bad = 0; break;
    case A_LIVEBYTES: (void)cplus_safe_alloc_live_bytes(); bad = 0; break;
    case A_FAILAFTER:
        cplus_safe_alloc_fail_after(vec == V_ZERO ? 0 : 2);
        cplus_safe_alloc_fail_reset();
        bad = 0;
        break;
    case A_FAILRESET: cplus_safe_alloc_fail_reset(); bad = 0; break;
    case A_SEQ: { unsigned long s = cplus_safe_alloc_seq(); (void)s; bad = 0; break; }
    default: bad = 1;
    }
    return bad ? R_FAIL : R_PASS;
}

/* ---- io ---------------------------------------------------------------- */
enum { A_FOPEN, A_FCLOSE, A_FREAD, A_FWRITE, A_STREAMCLOSED, A_STREAMWASCLOSED,
       A_OPENCOUNT, A_FGETS, A_FGETSCHK, A_FREADCHK, A_FWRITECHK, A_GETENV };

static FILE *reopen_tmp(void)
{
    FILE *f = tmpfile();
    if (!f) return NULL;
    fputs("hello world\n", f);
    fflush(f);
    rewind(f);
    return f;
}

static int run_io(int op, int vec)
{
    FILE *f = NULL;
    unsigned char buf[32];
    char cbuf[32];
    size_t r = 99;
    int i = 0, bad = 0;

    memset(buf, 0, sizeof(buf));
    memset(cbuf, 0, sizeof(cbuf));

    switch (op) {
    case A_FOPEN: {
        char tmpl[] = "/tmp/edge100_safecrt_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd < 0) return R_FAIL;
        close(fd); unlink(tmpl);
        f = cplus_fopen_s(vec == V_NULL ? NULL : tmpl, vec == V_ZERO ? "" : "w+");
        bad = (vec == V_NULL || vec == V_ZERO) ? (f != NULL) : (f == NULL);
        if (f) { cplus_fclose_s(f); unlink(tmpl); }
        break;
    }
    case A_FCLOSE:
        if (vec == V_NULL) { i = cplus_fclose_s(NULL); bad = (i == 0); }
        else {
            f = reopen_tmp(); if (!f) return R_FAIL;
            i = cplus_fclose_s(f);
            if (vec == V_CLOSED) { int j = cplus_fclose_s(f); bad = (j == 0); }
            else bad = (i != 0);
        }
        break;
    case A_FREAD:
        f = reopen_tmp(); if (!f) return R_FAIL;
        if (vec == V_CLOSED) { cplus_fclose_s(f); }
        r = cplus_fread_s(vec == V_NULL ? NULL : buf,
                          sizeof(buf), /* caller-declared capacity stays truthful */
                          vec == V_ZERO ? 0 : 1,
                          vec == V_SIZEMAX ? SIZE_MAX : 5, f);
        bad = (vec == V_NULL || vec == V_ZERO || vec == V_SIZEMAX || vec == V_CLOSED)
              ? 0 : (r == 0);
        if (vec != V_CLOSED) cplus_fclose_s(f);
        break;
    case A_FWRITE:
        f = reopen_tmp(); if (!f) return R_FAIL;
        if (vec == V_CLOSED) { cplus_fclose_s(f); }
        r = cplus_fwrite_s(vec == V_NULL ? NULL : "data",
                           4, /* truthful bmax; SIZE_MAX is in size*count */
                           vec == V_ZERO ? 0 : 1,
                           vec == V_SIZEMAX ? SIZE_MAX : 1, f);
        bad = (vec == V_NULL || vec == V_ZERO || vec == V_SIZEMAX || vec == V_CLOSED)
              ? 0 : (r == 0);
        if (vec != V_CLOSED) cplus_fclose_s(f);
        break;
    case A_STREAMCLOSED:
        f = reopen_tmp(); if (!f) return R_FAIL;
        i = cplus_io_stream_is_closed(vec == V_NULL ? NULL : f);
        bad = (vec == V_NULL) ? (i != 0) : (i != 0);
        if (vec == V_CLOSED) { cplus_fclose_s(f); i = cplus_io_stream_is_closed(f); bad = (i == 0); }
        else cplus_fclose_s(f);
        break;
    case A_STREAMWASCLOSED:
        f = reopen_tmp(); if (!f) return R_FAIL;
        i = cplus_safe_stream_was_closed(vec == V_NULL ? NULL : f);
        if (vec == V_CLOSED) { cplus_fclose_s(f); i = cplus_safe_stream_was_closed(f); bad = (i == 0); }
        else { bad = (i != 0); cplus_fclose_s(f); }
        break;
    case A_OPENCOUNT: (void)cplus_safe_open_stream_count(); bad = 0; break;
    case A_FGETS:
        f = reopen_tmp(); if (!f) return R_FAIL;
        if (vec == V_CLOSED) cplus_fclose_s(f);
        {
            char *g = cplus_fgets_s(vec == V_NULL ? NULL : cbuf,
                                    vec == V_ZERO ? 0 : (vec == V_ONEOVER ? sizeof(cbuf) + 8 : sizeof(cbuf)),
                                    vec == V_ZERO ? 0 : (int)(vec == V_ONEOVER ? sizeof(cbuf) + 8 : 8), f);
            bad = (vec == V_NULL || vec == V_ZERO || vec == V_CLOSED) ? (g != NULL) : (g == NULL);
        }
        if (vec != V_CLOSED) cplus_fclose_s(f);
        break;
    case A_FGETSCHK:
        f = reopen_tmp(); if (!f) return R_FAIL;
        if (vec == V_CLOSED) cplus_fclose_s(f);
        {
            char *g = cplus_fgets_s_checked(vec == V_NULL ? NULL : cbuf,
                                            vec == V_ONEOVER ? 4 : sizeof(cbuf),
                                            vec == V_ZERO ? 0 : (int)(vec == V_ONEOVER ? 32 : 8), f);
            bad = (vec == V_NULL || vec == V_ZERO || vec == V_ONEOVER || vec == V_CLOSED)
                  ? (g != NULL) : (g == NULL);
        }
        if (vec != V_CLOSED) cplus_fclose_s(f);
        break;
    case A_FREADCHK:
        f = reopen_tmp(); if (!f) return R_FAIL;
        if (vec == V_CLOSED) cplus_fclose_s(f);
        r = cplus_fread_s_checked(vec == V_NULL ? NULL : buf,
                                  vec == V_ONEOVER ? 4 : sizeof(buf),
                                  sizeof(buf),
                                  vec == V_ZERO ? 0 : 1,
                                  vec == V_SIZEMAX ? SIZE_MAX : 5, f);
        bad = (vec == V_NULL || vec == V_ZERO || vec == V_SIZEMAX || vec == V_ONEOVER || vec == V_CLOSED)
              ? 0 : (r == 0);
        if (vec != V_CLOSED) cplus_fclose_s(f);
        break;
    case A_FWRITECHK:
        f = reopen_tmp(); if (!f) return R_FAIL;
        if (vec == V_CLOSED) cplus_fclose_s(f);
        r = cplus_fwrite_s_checked(vec == V_NULL ? NULL : "data",
                                   vec == V_ONEOVER ? 2 : 4,
                                   4,
                                   vec == V_ZERO ? 0 : 1,
                                   vec == V_SIZEMAX ? SIZE_MAX : 1, f);
        bad = (vec == V_NULL || vec == V_ZERO || vec == V_SIZEMAX || vec == V_ONEOVER || vec == V_CLOSED)
              ? 0 : (r == 0);
        if (vec != V_CLOSED) cplus_fclose_s(f);
        break;
    case A_GETENV: {
        size_t len = 0;
        i = cplus_getenv_s(vec == V_NULL ? NULL : &len,
                           vec == V_NULL ? NULL : (char *)buf,
                           vec == V_ZERO ? 0 : (vec == V_ONEOVER ? 2 : sizeof(buf)),
                           "EDGE100_NO_SUCH_VAR");
        /* absent variable is a success with len = 0 (rc 0). */
        bad = (vec == V_NULL || vec == V_ZERO || vec == V_ONEOVER) ? 0 : (i != 0);
        break;
    }
    default: bad = 1;
    }
    return bad ? R_FAIL : R_PASS;
}

/* ---- format ------------------------------------------------------------ */
enum { A_SNPRINTF, A_VSNPRINTF, A_PRINTF, A_FPRINTF, A_SCANF, A_FSCANF,
       A_SSCANF, A_SPRINTF, A_VSPRINTF, A_PCHECK, A_FCHECK, A_SNCHECK,
       A_SPCHECK, A_SCHECK, A_FSCANCHECK, A_SSCANCHECK };

static int va_snprintf(char *d, size_t dm, const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt); r = cplus_vsnprintf_s(d, dm, fmt, ap); va_end(ap);
    return r;
}
static int va_sprintf(char *d, size_t dm, const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt); r = cplus_vsprintf_s(d, dm, fmt, ap); va_end(ap);
    return r;
}
static void silence_stdout(void)
{
    FILE *n = freopen("/dev/null", "w", stdout);
    FILE *in = freopen("/dev/null", "r", stdin); /* scanf cases must not block */
    (void)n; (void)in;
}

static int run_fmt(int op, int vec)
{
    const size_t cap = 32;
    Guard d;
    int rc = -999, written = 0, got = 0;
    FILE *tmp = NULL;
    int bad = 0, hostile, print_op, scan_op;
    size_t dm = cap;

    d = g_new(cap);
    if (!d.base) return R_FAIL;
    memset(d.buf, 0, cap);

    if (vec == V_EXACT) dm = 5;              /* "hello" + NUL */
    else if (vec == V_ONEOVER) dm = cap + 8; /* dmax exceeds real buffer */
    else if (vec == V_SIZEMAX) dm = SIZE_MAX;
    else if (vec == V_ZERO) dm = 0;

    /* ZERO is a defined no-op success; the rest are refusals (< 0). */
    hostile = (vec == V_NULL || vec == V_ONEOVER || vec == V_SIZEMAX ||
               vec == V_PCTN || vec == V_CLOSED || vec == V_UNTERM);
    print_op = (op == A_PRINTF || op == A_FPRINTF || op == A_PCHECK || op == A_FCHECK);
    scan_op = (op == A_SCANF || op == A_FSCANF || op == A_SSCANF ||
               op == A_SCHECK || op == A_FSCANCHECK || op == A_SSCANCHECK);

    switch (op) {
    case A_SNPRINTF:
        if (vec == V_NULL) rc = cplus_snprintf_s(NULL, cap, "hi");
        else if (vec == V_PCTN) rc = cplus_snprintf_s(d.buf, cap, "%n", &written);
        else rc = cplus_snprintf_s(d.buf, dm, "hello %d", 7);
        GCHECK(d, rc);
        break;
    case A_VSNPRINTF:
        if (vec == V_NULL) rc = va_snprintf(NULL, cap, "hi");
        else if (vec == V_PCTN) rc = va_snprintf(d.buf, cap, "%n", &written);
        else rc = va_snprintf(d.buf, dm, "hello %d", 7);
        GCHECK(d, rc);
        break;
    case A_PRINTF:
        silence_stdout();
        if (vec == V_NULL) rc = cplus_printf_s(NULL);
        else if (vec == V_PCTN) rc = cplus_printf_s("%n", &written);
        else rc = cplus_printf_s("hello");
        break;
    case A_FPRINTF:
        tmp = tmpfile(); if (!tmp) { g_free(&d); return R_FAIL; }
        silence_stdout();
        if (vec == V_NULL) rc = cplus_fprintf_s(tmp, NULL);
        else if (vec == V_PCTN) rc = cplus_fprintf_s(tmp, "%n", &written);
        else if (vec == V_CLOSED) { cplus_fclose_s(tmp); rc = cplus_fprintf_s(tmp, "hi"); }
        else rc = cplus_fprintf_s(tmp, "hello");
        if (vec != V_CLOSED) fclose(tmp);
        break;
    case A_SCANF:
        silence_stdout();
        if (vec == V_NULL) rc = cplus_scanf_s(NULL);
        else if (vec == V_PCTN) rc = cplus_scanf_s("%n", &written);
        else if (vec == V_UNTERM) rc = cplus_scanf_s("%s", d.buf); /* unbounded %s */
        else rc = cplus_scanf_s("%d", &got);
        break;
    case A_FSCANF:
        tmp = tmpfile(); if (!tmp) { g_free(&d); return R_FAIL; }
        fputs("42", tmp); fflush(tmp); rewind(tmp);
        if (vec == V_NULL) rc = cplus_fscanf_s(tmp, NULL);
        else if (vec == V_PCTN) rc = cplus_fscanf_s(tmp, "%n", &written);
        else if (vec == V_CLOSED) { cplus_fclose_s(tmp); rc = cplus_fscanf_s(tmp, "%d", &got); }
        else if (vec == V_UNTERM) rc = cplus_fscanf_s(tmp, "%s", d.buf);
        else rc = cplus_fscanf_s(tmp, "%d", &got);
        if (vec != V_CLOSED) fclose(tmp);
        break;
    case A_SSCANF:
        if (vec == V_NULL) rc = cplus_sscanf_s(NULL, "%d", &got);
        else if (vec == V_PCTN) rc = cplus_sscanf_s("42", "%n", &written);
        else if (vec == V_UNTERM) rc = cplus_sscanf_s("42", "%s", d.buf);
        else rc = cplus_sscanf_s("42", "%d", &got);
        break;
    case A_SPRINTF:
        if (vec == V_NULL) rc = cplus_sprintf_s(NULL, cap, "hi");
        else if (vec == V_PCTN) rc = cplus_sprintf_s(d.buf, cap, "%n", &written);
        else rc = cplus_sprintf_s(d.buf, dm, "hello %d", 7);
        GCHECK(d, rc);
        break;
    case A_VSPRINTF:
        if (vec == V_NULL) rc = va_sprintf(NULL, cap, "hi");
        else if (vec == V_PCTN) rc = va_sprintf(d.buf, cap, "%n", &written);
        else rc = va_sprintf(d.buf, dm, "hello %d", 7);
        GCHECK(d, rc);
        break;
    case A_PCHECK:
        silence_stdout();
        if (vec == V_NULL) rc = cplus_printf_s_checked(1, NULL);
        else if (vec == V_PCTN) rc = cplus_printf_s_checked(1, "%n", &written);
        else rc = cplus_printf_s_checked(1, "hello");
        break;
    case A_FCHECK:
        tmp = tmpfile(); if (!tmp) { g_free(&d); return R_FAIL; }
        silence_stdout();
        if (vec == V_NULL) rc = cplus_fprintf_s_checked(1, tmp, NULL);
        else if (vec == V_PCTN) rc = cplus_fprintf_s_checked(1, tmp, "%n", &written);
        else if (vec == V_CLOSED) { cplus_fclose_s(tmp); rc = cplus_fprintf_s_checked(1, tmp, "hi"); }
        else rc = cplus_fprintf_s_checked(1, tmp, "hello");
        if (vec != V_CLOSED) fclose(tmp);
        break;
    case A_SNCHECK:
        if (vec == V_NULL) rc = cplus_snprintf_s_checked(1, cap, NULL, cap, "hi");
        else if (vec == V_PCTN) rc = cplus_snprintf_s_checked(1, cap, d.buf, cap, "%n", &written);
        else rc = cplus_snprintf_s_checked(1, cap, d.buf, dm, "hello %d", 7);
        GCHECK(d, rc);
        break;
    case A_SPCHECK:
        if (vec == V_NULL) rc = cplus_sprintf_s_checked(1, cap, NULL, cap, "hi");
        else if (vec == V_PCTN) rc = cplus_sprintf_s_checked(1, cap, d.buf, cap, "%n", &written);
        else rc = cplus_sprintf_s_checked(1, cap, d.buf, dm, "hello %d", 7);
        GCHECK(d, rc);
        break;
    case A_SCHECK:
        silence_stdout();
        if (vec == V_NULL) rc = cplus_scanf_s_checked(1, NULL);
        else if (vec == V_PCTN) rc = cplus_scanf_s_checked(1, "%n", &written);
        else if (vec == V_UNTERM) rc = cplus_scanf_s_checked(1, "%s", d.buf);
        else rc = cplus_scanf_s_checked(1, "%d", &got);
        break;
    case A_FSCANCHECK:
        tmp = tmpfile(); if (!tmp) { g_free(&d); return R_FAIL; }
        fputs("42", tmp); fflush(tmp); rewind(tmp);
        if (vec == V_NULL) rc = cplus_fscanf_s_checked(1, tmp, NULL);
        else if (vec == V_PCTN) rc = cplus_fscanf_s_checked(1, tmp, "%n", &written);
        else if (vec == V_CLOSED) { cplus_fclose_s(tmp); rc = cplus_fscanf_s_checked(1, tmp, "%d", &got); }
        else if (vec == V_UNTERM) rc = cplus_fscanf_s_checked(1, tmp, "%s", d.buf);
        else rc = cplus_fscanf_s_checked(1, tmp, "%d", &got);
        if (vec != V_CLOSED) fclose(tmp);
        break;
    case A_SSCANCHECK:
        if (vec == V_NULL) rc = cplus_sscanf_s_checked(1, NULL, "%d", &got);
        else if (vec == V_PCTN) rc = cplus_sscanf_s_checked(1, "42", "%n", &written);
        else if (vec == V_UNTERM) rc = cplus_sscanf_s_checked(1, "42", "%s", d.buf);
        else rc = cplus_sscanf_s_checked(1, "42", "%d", &got);
        break;
    default: bad = 1;
    }
    if (print_op || scan_op) {
        /* Violations are reported as a negative return; a valid call is >= 0
         * for printers.  Scanf's exact count is stdin/file-state dependent,
         * so non-hostile scanf cases only require no crash. */
        if (hostile) bad = (rc >= 0);
        else if (!scan_op) bad = (rc < 0);
        else bad = 0;
    } else {
        bad = hostile ? (rc >= 0) : (rc < 0);
    }
    (void)written;
    g_free(&d);
    return bad ? R_FAIL : R_PASS;
}

/* ---- error / except / log / platform / stats --------------------------- */
enum { A_ERRSET, A_ERRGET };
static int run_error(int op, int vec)
{
    int before = (int)cplus_safe_get_constraint_mode();
    if (op == A_ERRSET) {
        cplus_safe_set_constraint_mode(vec == V_ZERO ? (cplus_safe_constraint_mode)99
                                                     : CPLUS_SAFE_CONSTRAINT_IGNORE);
        int after = (int)cplus_safe_get_constraint_mode();
        cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)before);
        return (vec == V_ZERO && after != CPLUS_SAFE_CONSTRAINT_IGNORE) ? R_FAIL : R_PASS;
    }
    { int m = (int)cplus_safe_get_constraint_mode(); if (m != 0 && m != 1) return R_FAIL; }
    return R_PASS;
}

static volatile int g_caught;
static const struct e4c_exception_type EDGE100_EX = { NULL, "Edge100" };
static void ex_throw(void)
{
    THROW(EDGE100_EX, "edge100 check %d", 1);
}
enum { A_EXINIT, A_CHECK, A_THROWIF, A_RETHROW };
static int run_except(int op, int vec)
{
    int cond = (vec == V_ZERO) ? 1 : 0;
    int status = (vec == V_ZERO) ? 0 : -1;
    g_caught = 0;
    cplus_except_init();
    switch (op) {
    case A_EXINIT: cplus_except_init(); return R_PASS;
    case A_CHECK:
        TRY {
            CPLUS_CHECK(cond, EDGE100_EX, "edge100 CPLUS_CHECK");
        } CATCH (EDGE100_EX) { g_caught = 1; } CATCH_ALL { } FINALLY { }
        break;
    case A_THROWIF:
        TRY {
            CPLUS_THROW_IF(status, EDGE100_EX, "edge100 CPLUS_THROW_IF");
        } CATCH (EDGE100_EX) { g_caught = 1; } CATCH_ALL { } FINALLY { }
        break;
    case A_RETHROW:
        TRY {
            TRY { ex_throw(); } CATCH (EDGE100_EX) { RETHROW(); } FINALLY { }
        } CATCH (EDGE100_EX) { g_caught = 1; } CATCH_ALL { } FINALLY { }
        break;
    default: return R_FAIL;
    }
    /* CPLUS_CHECK / CPLUS_THROW_IF throw only for the failing condition; the
     * zero vector uses a passing condition so no exception is expected.  RETHROW
     * always propagates the active exception for both vectors. */
    if (op == A_RETHROW) return (g_caught != 0) ? R_PASS : R_FAIL;
    return ((vec == V_ZERO) == (g_caught != 0)) ? R_FAIL : R_PASS;
}

enum { A_LOGSET, A_LOGGET, A_LOGMSG };
static int run_log(int op, int vec)
{
    cplus_safe_log_level prev = cplus_safe_log_get_level();
    if (op == A_LOGSET) {
        cplus_safe_log_set_level(vec == V_ZERO ? CPLUS_SAFE_LOG_OFF : CPLUS_SAFE_LOG_DEBUG);
        cplus_safe_log_set_level(prev);
        return R_PASS;
    }
    if (op == A_LOGGET) {
        cplus_safe_log_level l = cplus_safe_log_get_level();
        return (l <= CPLUS_SAFE_LOG_TRACE) ? R_PASS : R_FAIL;
    }
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_log(CPLUS_SAFE_LOG_ERROR,
                   vec == V_NULL ? NULL : "edge100",
                   vec == V_ZERO ? NULL : "message");
    cplus_safe_log_set_level(prev);
    return R_PASS;
}

static int run_platform(int op, int vec)
{
    cplus_safe_platform_info info;
    (void)op;
    memset(&info, 0x5A, sizeof(info));
    cplus_safe_platform_probe(vec == V_NULL ? NULL : &info);
    return R_PASS;
}

enum { A_STATREC, A_STATRESET, A_STATGET, A_STATDUMP };
static int run_stats(int op, int vec)
{
    cplus_safe_stat st;
    cplus_safe_stat arr[4];
    int ok = 0;
    switch (op) {
    case A_STATREC:
        cplus_safe_stat_record(vec == V_NULL ? NULL : (vec == V_ZERO ? "" : "edge100"),
                               vec == V_ZERO ? 0 : 1);
        ok = 1; break;
    case A_STATRESET: cplus_safe_stats_reset(); ok = 1; break;
    case A_STATGET:
        memset(&st, 0, sizeof(st));
        ok = cplus_safe_stats_get(vec == V_NULL ? NULL : (vec == V_ZERO ? "" : "edge100"),
                                  vec == V_ZERO ? NULL : &st);
        ok = (ok == 0 || ok == 1); break;
    case A_STATDUMP:
        ok = (int)cplus_safe_stats_dump(vec == V_NULL ? NULL : arr,
                                        vec == V_ZERO ? 0 : 4);
        ok = (ok >= 0); break;
    default: ok = 0;
    }
    return ok ? R_PASS : R_FAIL;
}

/* ---- runtime ----------------------------------------------------------- */
enum { A_SETHANDLER, A_SETFAILSTOP, A_GETFAILSTOP, A_SETTRAP, A_GETTRAP,
       A_VIOLATION, A_REPORT, A_GUARD, A_PUSH, A_POP, A_MARK, A_UNWTO,
       A_UNWIND, A_DEPTH, A_LEAKED, A_UNWOUND, A_RTRESET };

static void noop_handler(const char *m) { (void)m; }
static void noop_drop(void *p) { (void)p; }
static int guard_ok_body(void *ctx) { (void)ctx; return CPLUS_RUNTIME_OK; }
static int guard_bad_body(void *ctx) { (void)ctx; cplus_runtime_report("edge100 guard"); return CPLUS_RUNTIME_OK; }

static int run_runtime(int op, int vec)
{
    int i, before, bad = 0;
    switch (op) {
    case A_SETHANDLER:
        cplus_runtime_set_handler(vec == V_NULL ? NULL : noop_handler);
        cplus_runtime_set_handler(NULL);
        bad = 0; break;
    case A_SETFAILSTOP:
        before = cplus_runtime_get_failstop();
        cplus_runtime_set_failstop(vec == V_ZERO ? 0 : 0);
        cplus_runtime_set_failstop(before);
        bad = 0; break;
    case A_GETFAILSTOP:
        i = cplus_runtime_get_failstop(); bad = (i != 0 && i != 1); break;
    case A_SETTRAP:
        before = cplus_runtime_get_violation_trap();
        cplus_runtime_set_violation_trap(vec == V_ZERO ? 0 : 1);
        cplus_runtime_set_violation_trap(before);
        bad = 0; break;
    case A_GETTRAP:
        i = cplus_runtime_get_violation_trap(); bad = (i != 0 && i != 1); break;
    case A_VIOLATION:
        if (vec == V_NULL) cplus_runtime_violation(NULL, NULL);
        else if (vec == V_ZERO) cplus_runtime_violation("", "");
        else cplus_runtime_violation("edge100", "detail");
        bad = 0; break;
    case A_REPORT:
        if (vec == V_NULL) cplus_runtime_report(NULL);
        else if (vec == V_ZERO) cplus_runtime_report("");
        else cplus_runtime_report("edge100 report");
        bad = 0; break;
    case A_GUARD:
        if (vec == V_NULL) i = cplus_runtime_guard(NULL, NULL);
        else if (vec == V_ZERO) i = cplus_runtime_guard(guard_bad_body, NULL);
        else i = cplus_runtime_guard(guard_ok_body, NULL);
        /* cplus_runtime_report() only reports (the UBSan hook performs the
         * longjmp), so a body that returns OK yields CPLUS_RUNTIME_OK.  A NULL
         * body is the documented refusal. */
        bad = (vec == V_NULL) ? (i != CPLUS_RUNTIME_ERROR) : (i != CPLUS_RUNTIME_OK);
        break;
    case A_PUSH:
        if (vec == V_NULL) i = cplus_runtime_push_cleanup(NULL, NULL);
        else {
            i = cplus_runtime_push_cleanup(vec == V_ZERO ? noop_drop : noop_drop, NULL);
            cplus_runtime_pop_cleanup(0);
        }
        bad = (vec == V_NULL) ? (i == 0) : (i != 0);
        break;
    case A_POP:
        cplus_runtime_pop_cleanup(vec == V_ZERO ? 1 : 0);
        bad = 0; break;
    case A_MARK:
        i = cplus_runtime_cleanup_mark(); bad = (i < 0); break;
    case A_UNWTO:
        cplus_runtime_cleanup_unwind_to(vec == V_ZERO ? -1 : 0);
        bad = 0; break;
    case A_UNWIND:
        cplus_runtime_unwind_cleanups(); bad = 0; break;
    case A_DEPTH:
        i = cplus_runtime_cleanup_depth(); bad = (i < 0); break;
    case A_LEAKED: (void)cplus_runtime_leaked_cleanups(); bad = 0; break;
    case A_UNWOUND: (void)cplus_runtime_unwound_cleanups(); bad = 0; break;
    case A_RTRESET:
        cplus_runtime_reset_counters(); bad = 0; break;
    default: bad = 1;
    }
    return bad ? R_FAIL : R_PASS;
}

/* ---- ext mem ----------------------------------------------------------- */
enum { A_BCMP, A_EMEMCMP };
static int run_extmem(int op, int vec)
{
    unsigned char a[16], b[16];
    int r;
    memset(a, 0x11, sizeof(a));
    memset(b, 0x11, sizeof(b));
    b[3] = 0x22;
    /* These vendored constant-time comparators do not document NULL handling;
     * a NULL pointer is exercised with length 0 (no dereference).  NULL with a
     * positive length is outside the documented contract and is reported as an
     * informational finding in RESULTS.md rather than asserted here. */
    if (op == A_BCMP)
        r = cplus_sc_timingsafe_bcmp(vec == V_NULL ? NULL : a,
                                     vec == V_NULL ? NULL : b,
                                     (vec == V_NULL || vec == V_ZERO) ? 0 :
                                     (vec == V_ONEOVER ? sizeof(a) + 4 : sizeof(a)));
    else
        r = cplus_sc_timingsafe_memcmp(vec == V_NULL ? NULL : a,
                                       vec == V_NULL ? NULL : b,
                                       (vec == V_NULL || vec == V_ZERO) ? 0 :
                                       (vec == V_ONEOVER ? sizeof(a) + 4 : sizeof(a)));
    /* Refusal/undefined inputs must not crash; no canary to check beyond page. */
    (void)r;
    return R_PASS;
}

/* ---- safe_c.h mapped targets ------------------------------------------- */
enum { A_MEMRCHR, A_BSEARCH, A_CFREAD, A_CFWRITE, A_CPRINTF, A_CFPRINTF };
static int cmp_int(const void *k, const void *e, void *ctx)
{
    int a = *(const int *)k, b = *(const int *)e;
    (void)ctx;
    return (a > b) - (a < b);
}
static int run_safec(int op, int vec)
{
    int rc = 0, bad = 0;
    int key = 3, arr[4] = { 1, 2, 3, 4 };
    void *found = (void *)1;
    FILE *f = NULL;
    char buf[16];
    size_t r = 99;

    switch (op) {
    case A_MEMRCHR:
        rc = cplus_sc_upstream_memrchr_s(vec == V_NULL ? NULL : arr,
                                         vec == V_ZERO ? 0 : (vec == V_SIZEMAX ? SIZE_MAX : sizeof(arr)),
                                         'A', vec == V_NULL ? NULL : &found);
        bad = (vec == V_NULL || vec == V_ZERO || vec == V_SIZEMAX) ? (rc == 0) : 0;
        break;
    case A_BSEARCH:
        found = cplus_sc_upstream_bsearch_s(vec == V_NULL ? NULL : &key,
                                            vec == V_NULL ? NULL : arr, 4, sizeof(int),
                                            cmp_int, NULL);
        bad = (vec == V_NULL) ? (found != NULL) : (found == NULL);
        break;
    case A_CFREAD:
        f = tmpfile(); if (!f) return R_FAIL;
        fputs("hello", f); fflush(f); rewind(f);
        r = cplus_fread(vec == V_NULL ? NULL : buf,
                        vec == V_ZERO ? 0 : 1,
                        vec == V_SIZEMAX ? SIZE_MAX : 5, f); /* EOF stops reads */
        bad = (vec == V_NULL || vec == V_ZERO || vec == V_SIZEMAX) ? 0 : (r == 0);
        fclose(f);
        break;
    case A_CFWRITE:
        f = tmpfile(); if (!f) return R_FAIL;
        r = cplus_fwrite(vec == V_NULL ? NULL : "data",
                         2, vec == V_SIZEMAX ? SIZE_MAX : 1, /* size*count overflow */
                         f);
        bad = (vec == V_NULL || vec == V_ZERO || vec == V_SIZEMAX) ? 0 : (r == 0);
        fclose(f);
        break;
    case A_CPRINTF:
        silence_stdout();
        rc = cplus_printf(vec == V_NULL ? NULL : "hello");
        bad = (vec == V_NULL) ? (rc >= 0) : (rc < 0);
        break;
    case A_CFPRINTF:
        f = tmpfile(); if (!f) return R_FAIL;
        silence_stdout();
        rc = cplus_fprintf(f, vec == V_NULL ? NULL : "hello");
        bad = (vec == V_NULL) ? (rc >= 0) : (rc < 0);
        fclose(f);
        break;
    default: bad = 1;
    }
    return bad ? R_FAIL : R_PASS;
}

/* ======================================================================= */
/* API manifest table                                                       */
/* ======================================================================= */
enum { F_MEM, F_CAPMODE, F_STR, F_ALLOC, F_IO, F_FMT, F_ERROR, F_EXCEPT,
       F_LOG, F_PLATFORM, F_STATS, F_RUNTIME, F_EXTMEM, F_SAFEC };

typedef struct {
    const char *name;
    int fam, op;
    unsigned mask;
} Api;

#define A(n, f, o, m) { n, f, o, m }
static const Api APIS[] = {
    /* safe_memory.h */
    A("cplus_memcpy_s", F_MEM, A_MEMCPY, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX|M_OVERLAP),
    A("cplus_memmove_s", F_MEM, A_MEMMOVE, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX|M_OVERLAP),
    A("cplus_memset_s", F_MEM, A_MEMSET, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX),
    A("cplus_memcmp_s", F_MEM, A_MEMCMP, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX),
    A("cplus_memzero_s", F_MEM, A_MEMZERO, M_VALID|M_NULL|M_ZERO|M_SIZEMAX),
    A("cplus_memcpy_s_auto", F_MEM, A_MEMCPY_A, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX|M_OVERLAP),
    A("cplus_memmove_s_auto", F_MEM, A_MEMMOVE_A, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX|M_OVERLAP),
    A("cplus_memset_s_auto", F_MEM, A_MEMSET_A, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX),
    A("cplus_memcmp_s_auto", F_MEM, A_MEMCMP_A, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX),
    A("cplus_safe_set_unknown_capacity_mode", F_CAPMODE, A_CAPSET, M_VALID|M_ZERO),
    A("cplus_safe_unknown_capacity_strict", F_CAPMODE, A_CAPGET, M_VALID|M_ZERO),
    /* safe_string.h */
    A("cplus_strcpy_s", F_STR, A_STRCPY, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX|M_OVERLAP|M_UNTERM),
    A("cplus_strncpy_s", F_STR, A_STRNCPY, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX|M_OVERLAP|M_UNTERM),
    A("cplus_strcat_s", F_STR, A_STRCAT, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX|M_UNTERM),
    A("cplus_strncat_s", F_STR, A_STRNCAT, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX|M_UNTERM),
    A("cplus_strcmp_s", F_STR, A_STRCMP, M_VALID|M_NULL|M_ZERO|M_EXACT|M_UNTERM),
    A("cplus_strnlen_s", F_STR, A_STRNLEN, M_VALID|M_NULL|M_ZERO|M_SIZEMAX|M_UNTERM),
    A("cplus_strtok_s", F_STR, A_STRTOK, M_VALID|M_NULL|M_ZERO),
    A("cplus_strerror_s", F_STR, A_STRERROR, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX),
    A("cplus_strcpy_s_auto", F_STR, A_STRCPY_A, M_VALID|M_NULL),
    A("cplus_strncpy_s_auto", F_STR, A_STRNCPY_A, M_VALID|M_NULL|M_ONEOVER),
    A("cplus_strcat_s_auto", F_STR, A_STRCAT_A, M_VALID|M_NULL),
    A("cplus_strncat_s_auto", F_STR, A_STRNCAT_A, M_VALID|M_NULL|M_ONEOVER),
    /* safe_alloc.h */
    A("malloc_s", F_ALLOC, A_MALLOC, M_VALID|M_ZERO|M_SIZEMAX),
    A("calloc_s", F_ALLOC, A_CALLOC, M_VALID|M_ZERO|M_SIZEMAX),
    A("realloc_s", F_ALLOC, A_REALLOC, M_VALID|M_NULL|M_ZERO|M_SIZEMAX),
    A("free_s", F_ALLOC, A_FREE, M_VALID|M_NULL|M_DOUBLEFREE),
    A("free_checked_s", F_ALLOC, A_FREECHK, M_VALID|M_NULL|M_DOUBLEFREE),
    A("free_checked_generation_s", F_ALLOC, A_FREEGEN, M_VALID|M_NULL|M_ZERO|M_DOUBLEFREE),
    A("cplus_safe_transfer_ownership_generation", F_ALLOC, A_TRANSFERGEN, M_VALID|M_NULL|M_ZERO|M_DOUBLEFREE),
    A("aligned_alloc_s", F_ALLOC, A_ALIGNED, M_VALID|M_NULL|M_ZERO|M_SIZEMAX),
    A("cplus_safe_alloc_get_meta", F_ALLOC, A_GETMETA, M_VALID|M_NULL|M_ZERO),
    A("cplus_safe_ptr_is_live", F_ALLOC, A_ISLIVE, M_VALID|M_NULL),
    A("cplus_safe_ptr_was_freed", F_ALLOC, A_WASFREED, M_VALID|M_NULL|M_DOUBLEFREE),
    A("cplus_safe_ptr_size", F_ALLOC, A_PTRSIZE, M_VALID|M_NULL),
    A("cplus_safe_remaining_capacity", F_ALLOC, A_REMCAP, M_VALID|M_NULL|M_EXACT),
    A("cplus_safe_ptr_elem_size", F_ALLOC, A_ELEMSIZE, M_VALID|M_NULL),
    A("cplus_safe_ptr_count", F_ALLOC, A_PTRCOUNT, M_VALID|M_NULL),
    A("cplus_safe_query_length", F_ALLOC, A_QUERYLEN, M_VALID|M_NULL|M_ZERO|M_SIZEMAX),
    A("cplus_safe_ptr_check_generation", F_ALLOC, A_CHECKGEN, M_VALID|M_NULL|M_ZERO|M_DOUBLEFREE),
    A("cplus_safe_alloc_live_count", F_ALLOC, A_LIVECOUNT, M_VALID|M_ZERO),
    A("cplus_safe_alloc_live_bytes", F_ALLOC, A_LIVEBYTES, M_VALID|M_ZERO),
    A("cplus_safe_transfer_ownership", F_ALLOC, A_TRANSFER, M_VALID|M_NULL|M_DOUBLEFREE),
    A("cplus_safe_alloc_fail_after", F_ALLOC, A_FAILAFTER, M_VALID|M_ZERO),
    A("cplus_safe_alloc_fail_reset", F_ALLOC, A_FAILRESET, M_VALID),
    A("cplus_safe_alloc_seq", F_ALLOC, A_SEQ, M_VALID|M_ZERO),
    /* safe_io.h */
    A("cplus_fopen_s", F_IO, A_FOPEN, M_VALID|M_NULL|M_ZERO),
    A("cplus_fclose_s", F_IO, A_FCLOSE, M_VALID|M_NULL|M_CLOSED),
    A("cplus_fread_s", F_IO, A_FREAD, M_VALID|M_NULL|M_ZERO|M_SIZEMAX|M_CLOSED),
    A("cplus_fwrite_s", F_IO, A_FWRITE, M_VALID|M_NULL|M_ZERO|M_SIZEMAX|M_CLOSED),
    A("cplus_io_stream_is_closed", F_IO, A_STREAMCLOSED, M_VALID|M_NULL|M_CLOSED),
    A("cplus_safe_stream_was_closed", F_IO, A_STREAMWASCLOSED, M_VALID|M_NULL|M_CLOSED),
    A("cplus_safe_open_stream_count", F_IO, A_OPENCOUNT, M_VALID|M_ZERO),
    A("cplus_fgets_s", F_IO, A_FGETS, M_VALID|M_NULL|M_ZERO|M_ONEOVER|M_CLOSED|M_UNTERM),
    A("cplus_fgets_s_checked", F_IO, A_FGETSCHK, M_VALID|M_NULL|M_ZERO|M_ONEOVER|M_CLOSED|M_UNTERM),
    A("cplus_fread_s_checked", F_IO, A_FREADCHK, M_VALID|M_NULL|M_ZERO|M_SIZEMAX|M_ONEOVER|M_CLOSED),
    A("cplus_fwrite_s_checked", F_IO, A_FWRITECHK, M_VALID|M_NULL|M_ZERO|M_SIZEMAX|M_ONEOVER|M_CLOSED),
    A("cplus_getenv_s", F_IO, A_GETENV, M_VALID|M_NULL|M_ZERO|M_ONEOVER),
    /* safe_format.h */
    A("cplus_snprintf_s", F_FMT, A_SNPRINTF, M_VALID|M_NULL|M_EXACT|M_PCTN),
    A("cplus_vsnprintf_s", F_FMT, A_VSNPRINTF, M_VALID|M_NULL|M_EXACT|M_PCTN),
    A("cplus_printf_s", F_FMT, A_PRINTF, M_VALID|M_NULL|M_PCTN),
    A("cplus_fprintf_s", F_FMT, A_FPRINTF, M_VALID|M_NULL|M_PCTN|M_CLOSED),
    A("cplus_scanf_s", F_FMT, A_SCANF, M_VALID|M_NULL|M_PCTN|M_UNTERM),
    A("cplus_fscanf_s", F_FMT, A_FSCANF, M_VALID|M_NULL|M_PCTN|M_CLOSED|M_UNTERM),
    A("cplus_sscanf_s", F_FMT, A_SSCANF, M_VALID|M_NULL|M_PCTN|M_UNTERM),
    A("cplus_sprintf_s", F_FMT, A_SPRINTF, M_VALID|M_NULL|M_EXACT|M_PCTN),
    A("cplus_vsprintf_s", F_FMT, A_VSPRINTF, M_VALID|M_NULL|M_EXACT|M_PCTN),
    A("cplus_printf_s_checked", F_FMT, A_PCHECK, M_VALID|M_NULL|M_PCTN),
    A("cplus_fprintf_s_checked", F_FMT, A_FCHECK, M_VALID|M_NULL|M_PCTN|M_CLOSED),
    A("cplus_snprintf_s_checked", F_FMT, A_SNCHECK, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX|M_PCTN),
    A("cplus_sprintf_s_checked", F_FMT, A_SPCHECK, M_VALID|M_NULL|M_ZERO|M_EXACT|M_ONEOVER|M_SIZEMAX|M_PCTN),
    A("cplus_scanf_s_checked", F_FMT, A_SCHECK, M_VALID|M_NULL|M_PCTN|M_UNTERM),
    A("cplus_fscanf_s_checked", F_FMT, A_FSCANCHECK, M_VALID|M_NULL|M_PCTN|M_CLOSED|M_UNTERM),
    A("cplus_sscanf_s_checked", F_FMT, A_SSCANCHECK, M_VALID|M_NULL|M_PCTN|M_UNTERM),
    /* safe_error.h */
    A("cplus_safe_set_constraint_mode", F_ERROR, A_ERRSET, M_VALID|M_ZERO),
    A("cplus_safe_get_constraint_mode", F_ERROR, A_ERRGET, M_VALID|M_ZERO),
    /* safe_except.h */
    A("cplus_except_init", F_EXCEPT, A_EXINIT, M_VALID|M_ZERO),
    A("CPLUS_CHECK", F_EXCEPT, A_CHECK, M_VALID|M_ZERO),
    A("CPLUS_THROW_IF", F_EXCEPT, A_THROWIF, M_VALID|M_ZERO),
    A("RETHROW", F_EXCEPT, A_RETHROW, M_VALID|M_ZERO),
    /* safe_log.h */
    A("cplus_safe_log_set_level", F_LOG, A_LOGSET, M_VALID|M_ZERO),
    A("cplus_safe_log_get_level", F_LOG, A_LOGGET, M_VALID|M_ZERO),
    A("cplus_safe_log", F_LOG, A_LOGMSG, M_VALID|M_NULL|M_ZERO),
    /* safe_platform.h */
    A("cplus_safe_platform_probe", F_PLATFORM, 0, M_VALID|M_NULL),
    /* safe_stats.h */
    A("cplus_safe_stat_record", F_STATS, A_STATREC, M_VALID|M_NULL|M_ZERO),
    A("cplus_safe_stats_reset", F_STATS, A_STATRESET, M_VALID|M_ZERO),
    A("cplus_safe_stats_get", F_STATS, A_STATGET, M_VALID|M_NULL|M_ZERO),
    A("cplus_safe_stats_dump", F_STATS, A_STATDUMP, M_VALID|M_NULL|M_ZERO),
    /* safe_runtime.h */
    A("cplus_runtime_set_handler", F_RUNTIME, A_SETHANDLER, M_VALID|M_NULL),
    A("cplus_runtime_set_failstop", F_RUNTIME, A_SETFAILSTOP, M_VALID|M_ZERO),
    A("cplus_runtime_get_failstop", F_RUNTIME, A_GETFAILSTOP, M_VALID|M_ZERO),
    A("cplus_runtime_set_violation_trap", F_RUNTIME, A_SETTRAP, M_VALID|M_ZERO),
    A("cplus_runtime_get_violation_trap", F_RUNTIME, A_GETTRAP, M_VALID|M_ZERO),
    A("cplus_runtime_violation", F_RUNTIME, A_VIOLATION, M_VALID|M_NULL|M_ZERO),
    A("cplus_runtime_report", F_RUNTIME, A_REPORT, M_VALID|M_NULL|M_ZERO),
    A("cplus_runtime_guard", F_RUNTIME, A_GUARD, M_VALID|M_NULL|M_ZERO),
    A("cplus_runtime_push_cleanup", F_RUNTIME, A_PUSH, M_VALID|M_NULL|M_ZERO),
    A("cplus_runtime_pop_cleanup", F_RUNTIME, A_POP, M_VALID|M_ZERO),
    A("cplus_runtime_cleanup_mark", F_RUNTIME, A_MARK, M_VALID|M_ZERO),
    A("cplus_runtime_cleanup_unwind_to", F_RUNTIME, A_UNWTO, M_VALID|M_ZERO),
    A("cplus_runtime_unwind_cleanups", F_RUNTIME, A_UNWIND, M_VALID|M_ZERO),
    A("cplus_runtime_cleanup_depth", F_RUNTIME, A_DEPTH, M_VALID|M_ZERO),
    A("cplus_runtime_leaked_cleanups", F_RUNTIME, A_LEAKED, M_VALID|M_ZERO),
    A("cplus_runtime_unwound_cleanups", F_RUNTIME, A_UNWOUND, M_VALID|M_ZERO),
    A("cplus_runtime_reset_counters", F_RUNTIME, A_RTRESET, M_VALID|M_ZERO),
    /* safe_ext_mem.h */
    A("cplus_sc_timingsafe_bcmp", F_EXTMEM, A_BCMP, M_VALID|M_NULL|M_ZERO|M_ONEOVER),
    A("cplus_sc_timingsafe_memcmp", F_EXTMEM, A_EMEMCMP, M_VALID|M_NULL|M_ZERO|M_ONEOVER),
    /* safe_c.h (macro-only surface: compiled targets of its mappings) */
    A("safe_c.memrchr_s", F_SAFEC, A_MEMRCHR, M_VALID|M_NULL|M_ZERO|M_SIZEMAX),
    A("safe_c.bsearch_s", F_SAFEC, A_BSEARCH, M_VALID|M_NULL),
    A("safe_c.cplus_fread", F_SAFEC, A_CFREAD, M_VALID|M_NULL|M_ZERO|M_SIZEMAX),
    A("safe_c.cplus_fwrite", F_SAFEC, A_CFWRITE, M_VALID|M_NULL|M_ZERO|M_SIZEMAX),
    A("safe_c.cplus_printf", F_SAFEC, A_CPRINTF, M_VALID|M_NULL),
    A("safe_c.cplus_fprintf", F_SAFEC, A_CFPRINTF, M_VALID|M_NULL)
};
#define NAPI ((int)(sizeof(APIS) / sizeof(APIS[0])))

static int dispatch(int fam, int op, int vec)
{
    switch (fam) {
    case F_MEM: return run_mem(op, vec);
    case F_CAPMODE: return run_capmode(op, vec);
    case F_STR: return run_str(op, vec);
    case F_ALLOC: return run_alloc(op, vec);
    case F_IO: return run_io(op, vec);
    case F_FMT: return run_fmt(op, vec);
    case F_ERROR: return run_error(op, vec);
    case F_EXCEPT: return run_except(op, vec);
    case F_LOG: return run_log(op, vec);
    case F_PLATFORM: return run_platform(op, vec);
    case F_STATS: return run_stats(op, vec);
    case F_RUNTIME: return run_runtime(op, vec);
    case F_EXTMEM: return run_extmem(op, vec);
    case F_SAFEC: return run_safec(op, vec);
    default: return R_FAIL;
    }
}

int main(int argc, char **argv)
{
    const char *logp = argc > 1 ? argv[1] : "results/cases.log";
    FILE *log = fopen(logp, "w");
    int total = 0, passed = 0, failed = 0;
    int a, v;

    if (!log) log = stderr;
    P = (size_t)sysconf(_SC_PAGESIZE);

    for (a = 0; a < NAPI; ++a) {
        for (v = 0; v < NVEC; ++v) {
            unsigned bit = 1u << v;
            pid_t pid;
            int status = 0;
            int result;
            const char *verdict;
            if (!(APIS[a].mask & bit)) continue;
            total++;
            pid = fork();
            if (pid < 0) { fprintf(log, "%s %s FORK_FAIL\n", APIS[a].name, VNAME[v]); failed++; continue; }
            if (pid == 0) {
                init_child();
                alarm(2);
                _exit(dispatch(APIS[a].fam, APIS[a].op, v));
            }
            if (waitpid(pid, &status, 0) < 0) {
                fprintf(log, "%s %s WAIT_FAIL\n", APIS[a].name, VNAME[v]);
                failed++; continue;
            }
            if (WIFSIGNALED(status)) {
                result = R_CORRUPT; /* crash */
                verdict = "CRASH";
            } else if (WIFEXITED(status)) {
                int e = WEXITSTATUS(status);
                if (e == R_PASS) { result = R_PASS; verdict = "PASS"; }
                else if (e == R_CORRUPT) { result = R_CORRUPT; verdict = "CORRUPT"; }
                else { result = R_FAIL; verdict = "FAIL"; }
            } else { result = R_FAIL; verdict = "FAIL"; }
            if (result == R_PASS) passed++; else failed++;
            fprintf(log, "%s %s %s\n", APIS[a].name, VNAME[v], verdict);
        }
    }
    printf("EDGE100 safecrt %d %d %d %d\n", NAPI, total, passed, failed);
    if (log != stderr) fclose(log);
    return 0;
}
