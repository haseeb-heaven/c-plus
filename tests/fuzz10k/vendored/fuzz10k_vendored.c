/* fuzz10k_vendored.c -- deterministic 10k+ scenario fuzz over all 26
 * vendored cplus_v_* symbols in build/safe/libcplus_safecrt.a.
 *
 * Ownership: tests/fuzz10k/vendored/ only.  See NOTES.md for the exact
 * build/run command, PRNG seed and measured results.
 *
 * Design
 * ------
 *  * 26 functions x 500 scenarios = 13,000 distinct deterministic
 *    scenarios.  Every scenario reseeds a splitmix64 PRNG with
 *    SEED ^ (scenario_index * GOLDEN), so replay is independent of the
 *    order in which scenarios execute (important after a crash restart).
 *  * Bounded alphabet includes embedded NUL, control bytes and non-ASCII
 *    bytes; strings are 0..512 bytes inside 1024-byte canary-guarded
 *    buffers, so every NUL-terminated read stops inside the object.
 *  * dmax / smax values span 0, 1, exact-fit, one-over, RSIZE_MAX_STR+1
 *    and SIZE_MAX.  Extreme lengths are paired with a *known* destination
 *    object size so the checked wrapper must refuse (EOVERFLOW) instead of
 *    scanning/writing outside the guard.  Public mem and strl entry points
 *    (which have no object-size parameter) are bounded to the real object.
 *  * NULL is supplied for destination, source and out-parameters.
 *  * A silent constraint handler counts refusals; pre/post canaries detect
 *    corruption (including writes past the end after a refusal).
 *  * A forked child runs the scenarios and publishes per-scenario progress,
 *    status and an FNV-1a digest into an mmap(MAP_SHARED) region.  The
 *    parent detects signals (crash), nonzero child exit and stalls (hang),
 *    then restarts at the next scenario so the full corpus always runs.
 *
 * Evidence, not proof: a clean run is empirical evidence that these
 * inputs did not crash, hang or corrupt guards under this harness; it is
 * not a formal correctness proof for all inputs or environments.
 */

#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef MAP_ANON
#ifdef MAP_ANONYMOUS
#define MAP_ANON MAP_ANONYMOUS
#endif
#endif

/* ------------------------------------------------------------------ */
/* Exact signatures, copied from libc/install-include/cplus_vendor_*.h  */
/* (rsize_t == size_t and errno_t == int at the pinned safeclib commit). */
/* ------------------------------------------------------------------ */
/* cplus_vendor_mem.h */
extern int cplus_v_memchr_s(const void *dest, size_t dmax, int ch,
                            void **resultp);
extern int cplus_v_memccpy_s(void *dest, size_t dmax, const void *src, int c,
                             size_t n);
/* cplus_vendor_strchr.h */
extern int cplus_v__strchr_s_chk(const char *dest, size_t dmax, int ch,
                                 char **resultp, size_t destbos);
extern int cplus_v__strrchr_s_chk(const char *dest, size_t dmax, int ch,
                                  char **resultp, size_t destbos);
/* cplus_vendor_strstr.h */
extern int cplus_v__strstr_s_chk(char *dest, size_t dmax, const char *src,
                                 size_t slen, char **substringp,
                                 size_t destbos, size_t srcbos);
extern int cplus_v__strcasestr_s_chk(char *dest, size_t dmax, const char *src,
                                     size_t slen, char **substring,
                                     size_t destbos, size_t srcbos);
/* cplus_vendor_spn.h */
extern int cplus_v__strspn_s_chk(const char *dest, size_t dmax,
                                 const char *src, size_t slen, size_t *countp,
                                 size_t destbos, size_t srcbos);
extern int cplus_v__strcspn_s_chk(const char *dest, size_t dmax,
                                  const char *src, size_t slen, size_t *countp,
                                  size_t destbos, size_t srcbos);
/* cplus_vendor_strl.h */
extern size_t cplus_v_strlcat(char *dest, const char *src, size_t dsize);
extern size_t cplus_v_strlcpy(char *dest, const char *src, size_t dsize);
/* cplus_vendor_stp.h */
extern char *cplus_v__stpcpy_s_chk(char *dest, size_t dmax, const char *src,
                                   int *errp, size_t destbos, size_t srcbos);
extern char *cplus_v__stpncpy_s_chk(char *dest, size_t dmax, const char *src,
                                    size_t slen, int *errp, size_t destbos,
                                    size_t srcbos);
/* cplus_vendor_set.h */
extern int cplus_v__strnset_s_chk(char *dest, size_t dmax, int value,
                                  size_t n, size_t destbos);
extern int cplus_v__strset_s_chk(char *dest, size_t dmax, int value,
                                 size_t destbos);
extern size_t cplus_v__strnterminate_s_chk(char *dest, size_t dmax,
                                           size_t destbos);
/* cplus_vendor_case.h */
extern int cplus_v__strtolowercase_s_chk(char *dest, size_t dmax,
                                         size_t destbos);
extern int cplus_v__strtouppercase_s_chk(char *dest, size_t dmax,
                                         size_t destbos);
/* cplus_vendor_firstlast.h */
extern int cplus_v__strfirstchar_s_chk(char *dest, size_t dmax, char c,
                                       char **firstp, size_t destbos);
extern int cplus_v__strlastchar_s_chk(char *dest, size_t dmax, char c,
                                      char **lastp, size_t destbos);
extern int cplus_v__strfirstdiff_s_chk(const char *dest, size_t dmax,
                                       const char *src, size_t *resultp,
                                       size_t destbos);
extern int cplus_v__strlastdiff_s_chk(const char *dest, size_t dmax,
                                      const char *src, size_t *resultp,
                                      size_t destbos);
/* cplus_vendor_field.h */
extern int cplus_v__strcmpfld_s_chk(const char *dest, size_t dmax,
                                    const char *src, int *resultp,
                                    size_t destbos);
extern int cplus_v__strcpyfld_s_chk(char *dest, size_t dmax, const char *src,
                                    size_t slen, size_t destbos);
extern int cplus_v__strcoll_s_chk(const char *dest, size_t dmax,
                                  const char *src, int *resultp,
                                  size_t destbos);

/* Internal mem BOS-aware workers (present in the archive, not in the
 * public mem header).  Signatures are the upstream _memchr_s_chk /
 * _memccpy_s_chk forms from third_party/safeclib/src/extmem C sources. */
extern int cplus_v__memchr_s_chk(const void *dest, size_t dmax, int ch,
                                 void **resultp, size_t destbos);
extern int cplus_v__memccpy_s_chk(void *dest, size_t dmax, const void *src,
                                  int c, size_t n, size_t destbos,
                                  size_t srcbos);

/* Constraint-handler installation + violation-trap control from the
 * checked runtime (declared in stdlib/safe/safe_runtime.h / safe_lib.h). */
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(
    void (*handler)(const char *, void *, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(
    void (*handler)(const char *, void *, int));
extern void cplus_runtime_set_violation_trap(int enabled);

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */
#define SEED              UINT64_C(0xC0FFEE5AF10D5EED)
#define GOLDEN            UINT64_C(0x9E3779B97F4A7C15)
#define FUNC_COUNT        26u
#define SCENARIOS_PER_FN  500u
#define N_SCENARIOS       ((uint64_t)FUNC_COUNT * (uint64_t)SCENARIOS_PER_FN)

#define CANARY_LEN        16u
#define CANARY_BYTE       0xA5u
#define BUF_CAP           1024u          /* physical buffer capacity */
#define STR_LEN_MAX       512u           /* generated string length 0..512 */
#define RSIZE_MAX_STR_VAL ((size_t)(4UL << 10)) /* 4096 */
#define BOS_UNKNOWN       ((size_t)-1)

#define HANG_TICKS        400            /* 400 * 10ms = 4s no progress */
#define TICK_NS           10000000L      /* 10 ms */

/* ------------------------------------------------------------------ */
/* Function-name table (logging only)                                 */
/* ------------------------------------------------------------------ */
static const char *const FN_NAMES[FUNC_COUNT] = {
    "cplus_v_memchr_s",            "cplus_v_memccpy_s",
    "cplus_v__memchr_s_chk",       "cplus_v__memccpy_s_chk",
    "cplus_v__strchr_s_chk",       "cplus_v__strrchr_s_chk",
    "cplus_v__strstr_s_chk",       "cplus_v__strcasestr_s_chk",
    "cplus_v__strspn_s_chk",       "cplus_v__strcspn_s_chk",
    "cplus_v_strlcat",             "cplus_v_strlcpy",
    "cplus_v__stpcpy_s_chk",       "cplus_v__stpncpy_s_chk",
    "cplus_v__strnset_s_chk",      "cplus_v__strset_s_chk",
    "cplus_v__strnterminate_s_chk","cplus_v__strtolowercase_s_chk",
    "cplus_v__strtouppercase_s_chk","cplus_v__strfirstchar_s_chk",
    "cplus_v__strlastchar_s_chk",  "cplus_v__strfirstdiff_s_chk",
    "cplus_v__strlastdiff_s_chk",  "cplus_v__strcmpfld_s_chk",
    "cplus_v__strcpyfld_s_chk",    "cplus_v__strcoll_s_chk"
};

/* ------------------------------------------------------------------ */
/* Shared memory published by the child, consumed by the parent        */
/* ------------------------------------------------------------------ */
typedef struct {
    volatile uint64_t current;        /* scenario executing right now */
    volatile uint64_t completed;      /* scenarios finished by this child */
    volatile uint64_t handler_calls;  /* constraint-handler invocations */
    volatile uint64_t corruptions;    /* guard corruptions observed */
    volatile int      done;           /* child completed its whole range */
    volatile int      phase;          /* 0 setup, 1 in-call, 2 post-check */
    uint64_t          digests[N_SCENARIOS];
} shared_t;

static shared_t *g_sh;

/* Silent handler: counts refusals, prints nothing (keeps stdout clean). */
static void silent_handler(const char *msg, void *ptr, int err)
{
    (void)msg;
    (void)ptr;
    (void)err;
    if (g_sh != NULL)
        (void)__sync_fetch_and_add(&g_sh->handler_calls, 1u);
}

/* ------------------------------------------------------------------ */
/* Deterministic PRNG (splitmix64)                                    */
/* ------------------------------------------------------------------ */
static uint64_t rng_state;
static void rng_seed(uint64_t s) { rng_state = s; }
static uint64_t rng64(void)
{
    uint64_t z = (rng_state += GOLDEN);
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}
static uint32_t rnd32(void) { return (uint32_t)(rng64() >> 32); }
static size_t rnd_below(size_t n) { return n ? (size_t)(rng64() % (uint64_t)n) : 0u; }

/* ------------------------------------------------------------------ */
/* Bounded alphabet: NUL, ASCII letters/digits/space, controls, 0x7F,   */
/* non-ASCII high bytes.  NUL appears twice to force embedded NULs.     */
/* ------------------------------------------------------------------ */
static const unsigned char ALPHA[] = {
    0x00, 0x41, 0x42, 0x61, 0x62, 0x30, 0x31, 0x20, 0x09, 0x0A,
    0x0D, 0x01, 0x1F, 0x7F, 0x80, 0xC3, 0xFF, 0x2D, 0x5A, 0x00
};

/* ------------------------------------------------------------------ */
/* Canary-guarded buffers                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    unsigned char pre[CANARY_LEN];
    unsigned char data[BUF_CAP];
    unsigned char post[CANARY_LEN];
} gbuf;

static void buf_init(gbuf *b, unsigned char filler)
{
    memset(b->pre, CANARY_BYTE, CANARY_LEN);
    memset(b->post, CANARY_BYTE, CANARY_LEN);
    memset(b->data, filler, BUF_CAP);
}
static int buf_canaries_ok(const gbuf *b)
{
    size_t i;
    for (i = 0; i < CANARY_LEN; ++i)
        if (b->pre[i] != CANARY_BYTE || b->post[i] != CANARY_BYTE)
            return 0;
    return 1;
}

/* Generate a string of 0..512 bytes from the bounded alphabet, with an
 * occasional forced embedded NUL.  Always NUL-terminate inside data[]. */
static size_t gen_string(unsigned char *data)
{
    size_t len = rnd_below(STR_LEN_MAX + 1u);
    size_t i;
    for (i = 0; i < len; ++i)
        data[i] = ALPHA[rnd_below(sizeof ALPHA)];
    if (len > 2u && (rnd32() & 3u) == 0u)
        data[rnd_below(len)] = 0x00;       /* embedded NUL */
    data[len] = 0x00;
    data[len + 1u] = 0x00;                 /* extra terminator */
    return len;
}

/* Length / object-size schedules. */
static size_t pick_dmax(void)
{
    switch (rnd32() % 10u) {
        case 0: return 0u;
        case 1: return 1u;
        case 2: return BUF_CAP;                    /* exact physical fit */
        case 3: return BUF_CAP + 1u;               /* one-over physical */
        case 4: return (size_t)-1;                 /* SIZE_MAX */
        case 5: return RSIZE_MAX_STR_VAL + 1u;     /* 4097 -> ESLEMAX */
        case 6: return 2u + rnd_below(BUF_CAP - 1u);
        case 7: return STR_LEN_MAX + 1u;           /* one-over logical */
        case 8: return STR_LEN_MAX;
        default: return rnd_below(BUF_CAP + 1u);
    }
}
static size_t pick_slen(void)
{
    switch (rnd32() % 8u) {
        case 0: return 0u;
        case 1: return 1u;
        case 2: return BUF_CAP;
        case 3: return STR_LEN_MAX + 1u;           /* one-over logical */
        case 4: return (size_t)-1;                 /* SIZE_MAX */
        case 5: return RSIZE_MAX_STR_VAL + 1u;     /* 4097 -> ESLEMAX */
        case 6: return rnd_below(BUF_CAP + 1u);
        default: return STR_LEN_MAX;
    }
}
static size_t pick_bos(size_t len)
{
    switch (rnd32() % 4u) {
        case 0: return BUF_CAP;                    /* known, exact */
        case 1: return BOS_UNKNOWN;                /* BOS_UNKNOWN path */
        case 2: return BUF_CAP / 2u;               /* smaller bogus bos */
        default: return BUF_CAP;
    }
}
static size_t safe_bos(size_t len)
{
    /* Any over-length must be refused via a known object size so the
     * wrapper never scans/writes outside the 1024-byte buffer. */
    size_t b = pick_bos(len);
    if (len > BUF_CAP)
        b = BUF_CAP;
    return b;
}

/* ------------------------------------------------------------------ */
/* Digest helpers (FNV-1a 64; never hash raw pointer addresses)        */
/* ------------------------------------------------------------------ */
static void hash_u64(uint64_t *h, uint64_t v)
{
    int i;
    for (i = 0; i < 8; ++i) {
        *h ^= (unsigned char)(v >> (i * 8));
        *h *= UINT64_C(0x100000001B3);
    }
}
static void hash_bytes(uint64_t *h, const void *p, size_t n)
{
    const unsigned char *q = (const unsigned char *)p;
    while (n--) {
        *h ^= *q++;
        *h *= UINT64_C(0x100000001B3);
    }
}
static void hash_sign(uint64_t *h, int v)
{
    hash_u64(h, (uint64_t)((v > 0) - (v < 0) + 1)); /* -1/0/1 -> 0/1/2 */
}

typedef struct {
    int      rc;
    int      has_rc;
    size_t   uz;
    int      has_uz;
    long long ptr;      /* -1 NULL, -2 out-of-range, >=0 byte offset */
    int      has_ptr;
    int      errp;
    int      has_errp;
} result_t;

static long long norm_ptr(void *p, const unsigned char *base, size_t cap)
{
    uintptr_t a, b;
    if (p == NULL)
        return -1;
    a = (uintptr_t)p;
    b = (uintptr_t)base;
    if (a >= b && (a - b) <= (uintptr_t)cap)
        return (long long)(a - b);
    return -2;
}

/* ------------------------------------------------------------------ */
/* One scenario                                                        */
/* ------------------------------------------------------------------ */
static void run_scenario(uint64_t idx, shared_t *sh, FILE *log)
{
    unsigned fn = (unsigned)(idx % FUNC_COUNT);
    gbuf dst, srcb;
    unsigned char ch;
    int value;
    size_t dlen, slenstr, dmax, slen, dbos, sbos, nbytes;
    int null_dest, null_src, null_out;
    char *out_ptr = (char *)0;
    size_t out_sz = (size_t)0xDEADBEEFu;
    int out_int = -12345;
    int errp = -12345;
    uint64_t h, handler_before;
    int c1, c2;
    result_t r;
    int refusal;

    rng_seed(SEED ^ (idx * GOLDEN));
    sh->current = idx;
    sh->phase = 0;

    memset(&r, 0, sizeof r);

    buf_init(&dst, 0x5Au);
    buf_init(&srcb, 0x5Au);
    dlen = gen_string(dst.data);
    slenstr = gen_string(srcb.data);

    ch = ALPHA[rnd_below(sizeof ALPHA)];
    value = (int)(rnd32() % 600u) - 300;   /* includes >255 and negatives */
    null_dest = (rnd32() % 16u) == 0u;
    null_src  = (rnd32() % 16u) == 0u;
    null_out  = (rnd32() % 16u) == 0u;

    dmax = pick_dmax();
    slen = pick_slen();
    dbos = safe_bos(dmax);
    sbos = safe_bos(slen);
    nbytes = rnd_below(BUF_CAP + 1u);

    {
        char       *d  = null_dest ? NULL : (char *)dst.data;
        const char *s  = null_src  ? NULL : (const char *)srcb.data;
        char      **op = null_out  ? NULL : &out_ptr;
        size_t     *szp = null_out  ? NULL : &out_sz;
        int        *ip = null_out  ? NULL : &out_int;

        switch (fn) {
        case 0: /* cplus_v_memchr_s (no bos: bound dmax to object) */
            if (dmax > BUF_CAP) dmax = BUF_CAP;
            handler_before = sh->handler_calls;
            r.rc = cplus_v_memchr_s((const void *)d, dmax, (int)ch,
                                    (void **)op);
            r.has_rc = 1;
            r.ptr = norm_ptr(out_ptr, dst.data, BUF_CAP);
            r.has_ptr = 1;
            break;
        case 1: /* cplus_v_memccpy_s (no bos: bound dmax and n) */
            if (dmax > BUF_CAP) dmax = BUF_CAP;
            if (nbytes > BUF_CAP) nbytes = BUF_CAP;
            if (nbytes > dmax) nbytes = dmax;
            handler_before = sh->handler_calls;
            r.rc = cplus_v_memccpy_s(d, dmax, (const void *)s, (int)ch,
                                     nbytes);
            r.has_rc = 1;
            break;
        case 2: /* cplus_v__memchr_s_chk */
            r.ptr = -1;
            handler_before = sh->handler_calls;
            r.rc = cplus_v__memchr_s_chk((const void *)d, dmax, (int)ch,
                                         (void **)op, dbos);
            r.has_rc = 1;
            r.ptr = norm_ptr(out_ptr, dst.data, BUF_CAP);
            r.has_ptr = 1;
            break;
        case 3: /* cplus_v__memccpy_s_chk (bound n to object and dmax) */
            if (nbytes > BUF_CAP) nbytes = BUF_CAP;
            if (nbytes > dmax) nbytes = dmax;
            handler_before = sh->handler_calls;
            r.rc = cplus_v__memccpy_s_chk(d, dmax, (const void *)s, (int)ch,
                                          nbytes, dbos, sbos);
            r.has_rc = 1;
            break;
        case 4: /* cplus_v__strchr_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strchr_s_chk(s, dmax, (int)ch, op, dbos);
            r.has_rc = 1;
            r.ptr = norm_ptr(out_ptr, srcb.data, BUF_CAP);
            r.has_ptr = 1;
            break;
        case 5: /* cplus_v__strrchr_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strrchr_s_chk(s, dmax, (int)ch, op, dbos);
            r.has_rc = 1;
            r.ptr = norm_ptr(out_ptr, srcb.data, BUF_CAP);
            r.has_ptr = 1;
            break;
        case 6: /* cplus_v__strstr_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strstr_s_chk(d, dmax, s, slen, op, dbos, sbos);
            r.has_rc = 1;
            r.ptr = norm_ptr(out_ptr, dst.data, BUF_CAP);
            r.has_ptr = 1;
            break;
        case 7: /* cplus_v__strcasestr_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strcasestr_s_chk(d, dmax, s, slen, op, dbos, sbos);
            r.has_rc = 1;
            r.ptr = norm_ptr(out_ptr, dst.data, BUF_CAP);
            r.has_ptr = 1;
            break;
        case 8: /* cplus_v__strspn_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strspn_s_chk(d, dmax, s, slen, szp, dbos, sbos);
            r.has_rc = 1;
            r.uz = out_sz; r.has_uz = 1;
            break;
        case 9: /* cplus_v__strcspn_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strcspn_s_chk(d, dmax, s, slen, szp, dbos, sbos);
            r.has_rc = 1;
            r.uz = out_sz; r.has_uz = 1;
            break;
        case 10: /* cplus_v_strlcat (no bos: bound dsize to object) */
            if (dmax > BUF_CAP) dmax = BUF_CAP;
            handler_before = sh->handler_calls;
            r.uz = cplus_v_strlcat(d, s, dmax);
            r.has_uz = 1;
            break;
        case 11: /* cplus_v_strlcpy */
            if (dmax > BUF_CAP) dmax = BUF_CAP;
            handler_before = sh->handler_calls;
            r.uz = cplus_v_strlcpy(d, s, dmax);
            r.has_uz = 1;
            break;
        case 12: /* cplus_v__stpcpy_s_chk */
            handler_before = sh->handler_calls;
            {
                char *ret = cplus_v__stpcpy_s_chk(d, dmax, s,
                                                  null_out ? NULL : &errp,
                                                  dbos, sbos);
                r.ptr = norm_ptr(ret, dst.data, BUF_CAP);
                r.has_ptr = 1;
            }
            r.errp = errp; r.has_errp = 1;
            break;
        case 13: /* cplus_v__stpncpy_s_chk (clamp slen <= dmax for writes) */
            if (dmax <= BUF_CAP && slen > dmax) slen = dmax;
            handler_before = sh->handler_calls;
            {
                char *ret = cplus_v__stpncpy_s_chk(d, dmax, s, slen,
                                                  null_out ? NULL : &errp,
                                                  dbos, sbos);
                r.ptr = norm_ptr(ret, dst.data, BUF_CAP);
                r.has_ptr = 1;
            }
            r.errp = errp; r.has_errp = 1;
            break;
        case 14: /* cplus_v__strnset_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strnset_s_chk(d, dmax, value, slen, dbos);
            r.has_rc = 1;
            break;
        case 15: /* cplus_v__strset_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strset_s_chk(d, dmax, value, dbos);
            r.has_rc = 1;
            break;
        case 16: /* cplus_v__strnterminate_s_chk */
            handler_before = sh->handler_calls;
            r.uz = cplus_v__strnterminate_s_chk(d, dmax, dbos);
            r.has_uz = 1;
            break;
        case 17: /* cplus_v__strtolowercase_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strtolowercase_s_chk(d, dmax, dbos);
            r.has_rc = 1;
            break;
        case 18: /* cplus_v__strtouppercase_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strtouppercase_s_chk(d, dmax, dbos);
            r.has_rc = 1;
            break;
        case 19: /* cplus_v__strfirstchar_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strfirstchar_s_chk(d, dmax, (char)ch, op, dbos);
            r.has_rc = 1;
            r.ptr = norm_ptr(out_ptr, dst.data, BUF_CAP);
            r.has_ptr = 1;
            break;
        case 20: /* cplus_v__strlastchar_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strlastchar_s_chk(d, dmax, (char)ch, op, dbos);
            r.has_rc = 1;
            r.ptr = norm_ptr(out_ptr, dst.data, BUF_CAP);
            r.has_ptr = 1;
            break;
        case 21: /* cplus_v__strfirstdiff_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strfirstdiff_s_chk(d, dmax, s, szp, dbos);
            r.has_rc = 1;
            r.uz = out_sz; r.has_uz = 1;
            break;
        case 22: /* cplus_v__strlastdiff_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strlastdiff_s_chk(d, dmax, s, szp, dbos);
            r.has_rc = 1;
            r.uz = out_sz; r.has_uz = 1;
            break;
        case 23: /* cplus_v__strcmpfld_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strcmpfld_s_chk(d, dmax, s, ip, dbos);
            r.has_rc = 1;
            r.errp = out_int; r.has_errp = 1;
            break;
        case 24: /* cplus_v__strcpyfld_s_chk (clamp slen for writes) */
            if (dmax <= BUF_CAP && slen > dmax) slen = dmax;
            if (slen > BUF_CAP) slen = BUF_CAP;
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strcpyfld_s_chk(d, dmax, s, slen, dbos);
            r.has_rc = 1;
            break;
        case 25: /* cplus_v__strcoll_s_chk */
            handler_before = sh->handler_calls;
            r.rc = cplus_v__strcoll_s_chk(d, dmax, s, ip, dbos);
            r.has_rc = 1;
            r.errp = out_int; r.has_errp = 1;
            break;
        default:
            handler_before = sh->handler_calls;
            break;
        }
    }

    sh->phase = 2;
    c1 = buf_canaries_ok(&dst);
    c2 = buf_canaries_ok(&srcb);
    if (!c1 || !c2) {
        (void)__sync_fetch_and_add(&sh->corruptions, 1u);
        fprintf(log, "CORRUPTION %llu %s dst_ok=%d src_ok=%d\n",
                (unsigned long long)idx, FN_NAMES[fn], c1, c2);
    }

    refusal = (sh->handler_calls != handler_before);

    /* Deterministic per-scenario digest. */
    h = UINT64_C(0xCBF29CE484222325);
    hash_u64(&h, idx);
    hash_u64(&h, (uint64_t)fn);
    hash_u64(&h, (uint64_t)dlen);
    hash_u64(&h, (uint64_t)slenstr);
    hash_u64(&h, (uint64_t)dmax);
    hash_u64(&h, (uint64_t)slen);
    hash_u64(&h, (uint64_t)dbos);
    hash_u64(&h, (uint64_t)sbos);
    hash_u64(&h, (uint64_t)null_dest << 2 | (uint64_t)null_src << 1 |
                  (uint64_t)null_out);
    hash_u64(&h, (uint64_t)ch);
    hash_u64(&h, (uint64_t)value);
    hash_bytes(&h, dst.data, dlen);
    hash_bytes(&h, srcb.data, slenstr);
    if (r.has_rc) hash_u64(&h, (uint64_t)r.rc);
    if (r.has_uz) hash_u64(&h, (uint64_t)r.uz);
    if (r.has_ptr) hash_u64(&h, (uint64_t)r.ptr);
    if (r.has_errp) hash_sign(&h, r.errp);
    hash_u64(&h, (uint64_t)refusal);
    hash_u64(&h, (uint64_t)c1 << 1 | (uint64_t)c2);
    sh->digests[idx] = h;

    fprintf(log,
            "%llu,%s,%lu,%lu,%lu,%lu,%d%d%d,%d,%d,%lld,%d,%d,%016llx\n",
            (unsigned long long)idx, FN_NAMES[fn],
            (unsigned long)dlen, (unsigned long)slenstr,
            (unsigned long)dmax, (unsigned long)slen,
            null_dest, null_src, null_out,
            r.has_rc ? r.rc : 0,
            r.has_uz ? (int)(r.uz > 1000000u ? 1000000u : r.uz) : 0,
            r.has_ptr ? r.ptr : -3LL,
            refusal, (c1 && c2),
            (unsigned long long)h);

    sh->completed = idx + 1u;
}

/* ------------------------------------------------------------------ */
/* Child process: run [start, N)                                       */
/* ------------------------------------------------------------------ */
static void child_main(uint64_t start, shared_t *sh)
{
    FILE *log;
    uint64_t i;

    /* Keep stdout/stderr of the child quiet; diagnostics go to a file. */
    {
        int devnull = open("/dev/null", O_WRONLY);
        int errfile = open("logs/child_stderr.log", O_WRONLY | O_CREAT | O_APPEND,
                           0644);
        if (devnull >= 0) { (void)dup2(devnull, STDOUT_FILENO); (void)close(devnull); }
        if (errfile >= 0) { (void)dup2(errfile, STDERR_FILENO); (void)close(errfile); }
    }

    cplus_runtime_set_violation_trap(0);
    cplus_sc_set_mem_constraint_handler_s_wrapper(silent_handler);
    cplus_sc_set_str_constraint_handler_s_wrapper(silent_handler);

    log = fopen("scenarios.log", "a");
    if (log == NULL)
        _exit(90);

    for (i = start; i < N_SCENARIOS; ++i) {
        sh->current = i;
        run_scenario(i, sh, log);
    }

    fclose(log);
    sh->done = 1;
}

/* ------------------------------------------------------------------ */
/* Parent                                                              */
/* ------------------------------------------------------------------ */
int main(void)
{
    shared_t *sh;
    uint64_t start = 0, total_refusals = 0, total_corruptions = 0;
    uint64_t crashes = 0, hangs = 0;
    uint64_t digest, i;
    FILE *logfile, *df;

    sh = (shared_t *)mmap(NULL, sizeof(*sh), PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANON, -1, 0);
    if (sh == (void *)-1) {
        perror("mmap");
        return 2;
    }
    g_sh = sh;

    /* Fresh log and digest for this run. */
    logfile = fopen("scenarios.log", "w");
    if (logfile != NULL) fclose(logfile);

    while (start < N_SCENARIOS) {
        pid_t pid;
        int status = 0, did_hang = 0;
        uint64_t last = 0, at;
        int idle = 0;

        sh->handler_calls = 0;
        sh->corruptions = 0;
        sh->done = 0;
        sh->phase = 0;
        sh->current = start;
        sh->completed = start;

        pid = fork();
        if (pid < 0) {
            perror("fork");
            return 2;
        }
        if (pid == 0) {
            child_main(start, sh);
            _exit(0);
        }

        while (!sh->done) {
            uint64_t c = sh->completed;
            struct timespec ts;
            if (c != last) { last = c; idle = 0; }
            else ++idle;
            if (idle >= HANG_TICKS) {
                (void)kill(pid, SIGKILL);
                did_hang = 1;
                break;
            }
            ts.tv_sec = 0;
            ts.tv_nsec = TICK_NS;
            (void)nanosleep(&ts, NULL);
        }
        (void)waitpid(pid, &status, 0);

        total_refusals += sh->handler_calls;
        total_corruptions += sh->corruptions;
        at = sh->current;

        if (did_hang) {
            fprintf(stderr, "HANG: scenario %llu made no progress; skipping\n",
                    (unsigned long long)at);
            ++hangs;
            start = (at < start ? start : at) + 1u;
            continue;
        }
        if (WIFSIGNALED(status)) {
            fprintf(stderr,
                    "CRASH: scenario %llu terminated by signal %d\n",
                    (unsigned long long)at, WTERMSIG(status));
            ++crashes;
            start = at + 1u;
            continue;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            fprintf(stderr,
                    "FAILURE: scenario %llu child exit %d\n",
                    (unsigned long long)at, WEXITSTATUS(status));
            ++crashes;
            start = at + 1u;
            continue;
        }
        if (!sh->done) {
            fprintf(stderr,
                    "FAILURE: scenario %llu child ended before completion\n",
                    (unsigned long long)at);
            ++crashes;
            start = at + 1u;
            continue;
        }
        start = N_SCENARIOS;   /* clean child did the whole remainder */
    }

    /* Fold per-scenario digests in scenario order (restart-independent). */
    digest = UINT64_C(0xCBF29CE484222325);
    for (i = 0; i < N_SCENARIOS; ++i)
        hash_u64(&digest, sh->digests[i]);

    df = fopen("digest.txt", "w");
    if (df != NULL) {
        fprintf(df, "seed=0x%016llx\n", (unsigned long long)SEED);
        fprintf(df, "functions=%u\n", FUNC_COUNT);
        fprintf(df, "scenarios=%llu\n", (unsigned long long)N_SCENARIOS);
        fprintf(df, "digest=%016llx\n", (unsigned long long)digest);
        fprintf(df, "crashes=%llu\n", (unsigned long long)crashes);
        fprintf(df, "hangs=%llu\n", (unsigned long long)hangs);
        fprintf(df, "corruptions=%llu\n", (unsigned long long)total_corruptions);
        fprintf(df, "refusals=%llu\n", (unsigned long long)total_refusals);
        fclose(df);
    }

    printf("FUZZ10K vendored %llu %llu %llu %llu %llu\n",
           (unsigned long long)N_SCENARIOS,
           (unsigned long long)crashes,
           (unsigned long long)hangs,
           (unsigned long long)total_corruptions,
           (unsigned long long)total_refusals);

    (void)munmap(sh, sizeof(*sh));
    return (crashes || hangs || total_corruptions) ? 1 : 0;
}
