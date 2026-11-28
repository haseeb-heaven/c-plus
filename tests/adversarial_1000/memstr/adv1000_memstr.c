/*
 * adv1000_memstr.c -- 1000 deterministic adversarial scenarios against the
 * checked memory/string APIs in build/safe/libcplus_safecrt.a.
 *
 * Families covered (both are in the single shipped archive):
 *   cplus_sc_*  : safeclib-backed wrappers (BOS_UNKNOWN -> all runtime checks)
 *   cplus_*_s   : intel-safestringlib-backed adapters
 *
 * Every scenario uses a fixed-seed xorshift64* PRNG (no wall clock, no time()).
 * Dest buffers are canary-filled; the canary starts at the declared capacity
 * (dmax/cap) and must be untouched after the call.  Hostile cases assert the
 * documented safeclib refusal code (400/402/403/404/406/407).
 *
 * Output: one "S <id> <api> <case> <rc> <canary_ok>" line per scenario and a
 * final "ADV1000 memstr 1000 <refused> <canary_ok> <failures>" aggregate.
 *
 * HONESTY: 1000 passing scenarios are EVIDENCE of safe refusal for these
 * specific deterministic inputs, NOT a proof of full memory safety.  These
 * APIs do not establish pointer provenance, ownership, lifetime or alias/UAF
 * safety; those remain the caller's and compiler's responsibility.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int errno_t;
typedef size_t rsize_t;

/* ------------------------------------------------------------------ */
/* API declarations (verified with nm against build/safe/libcplus_safecrt.a) */
/* ------------------------------------------------------------------ */
extern errno_t cplus_sc_memcpy_s(void *, rsize_t, const void *, rsize_t);
extern errno_t cplus_sc_memmove_s(void *, rsize_t, const void *, rsize_t);
extern errno_t cplus_sc_memset_s(void *, rsize_t, int, rsize_t);
extern errno_t cplus_sc_memcmp_s(const void *, rsize_t, const void *, rsize_t, int *);
extern errno_t cplus_sc_memzero_s(void *, rsize_t);
extern errno_t cplus_sc_strcpy_s(char *, rsize_t, const char *);
extern errno_t cplus_sc_strncpy_s(char *, rsize_t, const char *, rsize_t);
extern errno_t cplus_sc_strcat_s(char *, rsize_t, const char *);
extern errno_t cplus_sc_strncat_s(char *, rsize_t, const char *, rsize_t);
extern rsize_t  cplus_sc_strnlen_s(const char *, rsize_t);
extern errno_t cplus_sc_strcmp_s(const char *, rsize_t, const char *, int *);
extern errno_t cplus_sc_strerror_s(char *, rsize_t, int);
extern char   *cplus_sc_strtok_s(char *, rsize_t *, const char *, char **);

extern errno_t cplus_memcpy_s(void *, rsize_t, const void *, rsize_t);
extern errno_t cplus_memmove_s(void *, rsize_t, const void *, rsize_t);
extern errno_t cplus_memset_s(void *, rsize_t, int, rsize_t);
extern errno_t cplus_memcmp_s(const void *, rsize_t, const void *, rsize_t, int *);
extern errno_t cplus_memzero_s(void *, rsize_t);
extern errno_t cplus_strcpy_s(char *, rsize_t, const char *);
extern errno_t cplus_strncpy_s(char *, rsize_t, const char *, rsize_t);
extern errno_t cplus_strcat_s(char *, rsize_t, const char *);
extern errno_t cplus_strncat_s(char *, rsize_t, const char *, rsize_t);
extern rsize_t  cplus_strnlen_s(const char *, rsize_t);
extern errno_t cplus_strcmp_s(const char *, rsize_t, const char *, int *);
extern errno_t cplus_strerror_s(char *, rsize_t, int);
extern char   *cplus_strtok_s(char *, rsize_t *, const char *, char **);

extern void cplus_safe_set_constraint_mode(int);
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(void (*)(const char *, void *, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*)(const char *, void *, int));

/* ------------------------------------------------------------------ */
enum { NAPI = 13, NFAM = 2, NCASE = 7, NSCEN = 1000 };
enum { A_MEMCPY, A_MEMMOVE, A_MEMSET, A_MEMCMP, A_MEMZERO,
       A_STRCPY, A_STRNCPY, A_STRCAT, A_STRNCAT, A_STRCMP,
       A_STRNLEN, A_STRTOK, A_STRERROR };
enum { C_VALID, C_NULL_DEST, C_NULL_SRC, C_ONE_OVER, C_HUGE_N,
       C_OVERLAP, C_UNTERMINATED };

#define MAXLEN 512
#define GUARD  32
#define BSIZE  (MAXLEN + 1 + GUARD)
#define CAN    0xC3u
#define HUGE   ((rsize_t)-1)

/* Documented safeclib refusal codes (third_party/safeclib/include/safe_lib_errno.h) */
#define ESNULLP_  400
#define ESLEMIN_  402
#define ESLEMAX_  403
#define ESOVRLP_  404
#define ESNOSPC_  406
#define ESUNTERM_ 407

static const char *api_name[NAPI] = {
    "memcpy_s", "memmove_s", "memset_s", "memcmp_s", "memzero_s",
    "strcpy_s", "strncpy_s", "strcat_s", "strncat_s", "strcmp_s",
    "strnlen_s", "strtok_s", "strerror_s"
};
static const char *fam_name[NFAM] = { "cplus_sc", "cplus" };
static const char *case_name[NCASE] = {
    "valid", "null_dest", "null_src", "one_over", "huge_n",
    "overlap", "unterminated"
};

/* Which hostile/valid cases actually apply to each API. */
static const unsigned char applicable[NAPI][NCASE] = {
    /* memcpy_s  */ { 1,1,1,1,1,1,0 },
    /* memmove_s */ { 1,1,1,1,1,0,0 },  /* overlap is defined for memmove */
    /* memset_s  */ { 1,1,0,1,1,0,0 },  /* no src */
    /* memcmp_s  */ { 1,1,1,1,1,0,0 },  /* read-only: overlap not refused */
    /* memzero_s */ { 1,1,0,0,1,0,0 },  /* no length argument */
    /* strcpy_s  */ { 1,1,1,1,1,1,0 },
    /* strncpy_s */ { 1,1,1,1,1,1,0 },
    /* strcat_s  */ { 1,1,1,1,1,0,1 },
    /* strncat_s */ { 1,1,1,1,1,0,1 },
    /* strcmp_s  */ { 1,1,1,0,1,0,0 },  /* bounded compare never ESUNTERM/ESNOSPC here */
    /* strnlen_s */ { 1,1,0,0,1,0,0 },  /* count query: 0 on constraint violation */
    /* strtok_s  */ { 1,1,0,0,1,0,0 },  /* token query: NULL + errno on violation */
    /* strerror_s*/ { 1,1,0,1,1,0,0 }
};

/* ------------------------------------------------------------------ */
/* Fixed-seed PRNG: xorshift64*                                        */
/* ------------------------------------------------------------------ */
static uint64_t g_rng = UINT64_C(0x5EED1234ABCD5678);
static uint64_t rnd64(void) {
    uint64_t x = g_rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rng = x;
    return x * UINT64_C(2685821657736338717);
}
static size_t rnd_range(size_t lo, size_t hi) { /* inclusive */
    return lo + (size_t)(rnd64() % (uint64_t)(hi - lo + 1));
}

static unsigned char dest[BSIZE], src[BSIZE];
static int diff_out, ind_out;
static rsize_t dmax_out;
static char *tok_out;

static void fill(unsigned char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; i++) p[i] = v;
}

static int canary_ok(size_t cap) {
    size_t i;
    if (cap > MAXLEN) return 0;
    for (i = cap; i < cap + GUARD; i++)
        if (dest[i] != CAN) return 0;
    return 1;
}

/* Build fresh canary-filled buffers for a scenario. */
static void reset_buffers(size_t cap, size_t slen, int terminate_src) {
    fill(dest, sizeof dest, CAN);
    fill(src, sizeof src, 0x41);
    src[slen] = terminate_src ? 0 : (unsigned char)0x41;
    diff_out = -777;
    ind_out = -777;
    dmax_out = (rsize_t)cap;
    tok_out = NULL;
}

static void noop_handler(const char *m, void *p, int e) {
    (void)m; (void)p; (void)e;
}

/* ------------------------------------------------------------------ */
/* Execute one scenario. Returns: 1 if rc + canary matched, 0 otherwise. */
/* Writes observed rc (long long) and canary flag through pointers.      */
/* ------------------------------------------------------------------ */
static int run_scenario(int api, int fam, int cas,
                        size_t D, size_t L, size_t O,
                        long long *rc_out, int *can_out, int *hostile_out) {
    rsize_t dm, n, cap;
    unsigned char *dp;
    const unsigned char *sp;
    int expect;      /* expected numeric rc (normalized) */
    int hostile;
    int is_valid = (cas == C_VALID);
    int is_nlen = (api == A_STRNLEN);
    int is_tok  = (api == A_STRTOK);
    long long rc = 0;
    size_t slen;

    /* Effective declared capacity used for the canary boundary. */
    cap = D ? D : 1;
    dm = (rsize_t)cap;
    n = (rsize_t)(L % (cap + 1));
    slen = L % (cap + 1);
    sp = src;
    dp = dest;
    expect = 0;
    hostile = !is_valid;

    switch (cas) {
    case C_VALID:
        dm = (rsize_t)cap;
        if (api == A_STRTOK) {
            if (cap < 8) { cap = 8; dm = 8; }
            dmax_out = (rsize_t)cap;
        } else if (api == A_STRERROR) {
            if (cap < 16) { cap = 16; dm = 16; }
            dmax_out = (rsize_t)cap;
        }
        reset_buffers(cap, slen, 1);
        if (api == A_MEMCPY || api == A_MEMMOVE || api == A_MEMCMP ||
            api == A_MEMSET) {
            n = (rsize_t)(L % (cap + 1));
        } else if (api == A_STRCPY) {
            src[slen] = 0;                 /* strlen == slen < dmax */
        } else if (api == A_STRNCPY) {
            n = (rsize_t)(L % cap);        /* 0..dmax-1 */
        } else if (api == A_STRCAT || api == A_STRNCAT) {
            dest[0] = 0;                   /* empty, NUL-terminated dest */
            if (api == A_STRNCAT) {
                n = (rsize_t)(L % cap);
            } else {
                src[slen] = 0;             /* slen <= cap-1 for strcat valid */
                if (slen == cap) src[cap - 1] = 0;
            }
        } else if (api == A_STRCMP) {
            dest[0] = 0;
            src[slen] = 0;
        } else if (api == A_STRNLEN) {
            dest[slen] = 0;                /* NUL lives in the probed string */
            expect = (int)slen;            /* documented count result */
        } else if (api == A_STRTOK) {
            dest[0] = 'a'; dest[1] = ' '; dest[2] = 'b'; dest[3] = 0;
            expect = 0;                    /* 0 == non-NULL token found */
        } else if (api == A_STRERROR) {
            expect = 0;
        }
        break;

    case C_NULL_DEST:
        dm = (rsize_t)cap;
        n = (rsize_t)((L % cap) + 1);      /* non-zero: no slen==0 short-circuit */
        reset_buffers(cap, slen, 1);
        dp = NULL;
        expect = ESNULLP_;
        if (is_nlen) expect = 0;           /* strnlen_s returns 0 on NULL */
        if (is_tok) {
            dmax_out = (rsize_t)cap;
            tok_out = NULL;                /* NULL context -> ESNULLP via errno */
            expect = ESNULLP_;
        }
        break;

    case C_NULL_SRC:
        dm = (rsize_t)cap;
        n = (rsize_t)((L % cap) + 1);
        reset_buffers(cap, slen, 1);
        sp = NULL;
        expect = ESNULLP_;
        break;

    case C_ONE_OVER:
        dm = (rsize_t)cap;
        reset_buffers(cap, slen, 1);
        /* n = dmax + 1 + up to 63 extra: unchecked API would corrupt dest. */
        n = (rsize_t)(cap + 1 + (L % 63));
        expect = (api == A_STRERROR) ? ESLEMIN_ : ESNOSPC_;
        if (api == A_STRERROR) {
            dm = (rsize_t)(1 + (O % 3));   /* dmax in 1..3 => ESLEMIN for long msg */
            cap = (size_t)dm;
            dmax_out = dm;
        } else if (api == A_STRCAT || api == A_STRNCAT) {
            dest[0] = 0;                   /* valid dest, src too long */
            if (api == A_STRNCAT) {
                fill(src, cap, 0x41);      /* strlen(src) == dmax -> ESNOSPC */
                src[cap] = 0;
                n = (rsize_t)(cap + 1);
            } else {
                fill(src, cap, 0x41);      /* strlen(src) == dmax -> ESNOSPC */
                src[cap] = 0;
            }
        } else if (api == A_STRCPY) {
            fill(src, cap, 0x41);          /* strlen == dmax -> needs dmax+1 */
            src[cap] = 0;
        } else if (api == A_STRNCPY) {
            /* strlen(src) >= dmax is required to force ESNOSPC; otherwise a
             * short source succeeds regardless of the slen argument. */
            fill(src, cap + 1, 0x41);
            src[cap + 1] = 0;
            n = (rsize_t)(cap + 1);
        }
        break;

    case C_HUGE_N:
        reset_buffers(cap, slen, 1);
        expect = ESLEMAX_;
        if (api == A_MEMZERO) {
            dm = HUGE;                     /* dmax itself is the huge input */
        } else if (api == A_MEMCPY || api == A_MEMMOVE || api == A_MEMCMP ||
                   api == A_MEMSET || api == A_STRNCPY || api == A_STRNCAT) {
            n = HUGE;
        } else {
            dm = HUGE;                     /* strcpy/strcat/strcmp/strerror */
        }
        if (is_nlen) { dm = HUGE; expect = 0; }        /* count query: 0 */
        if (is_tok) {
            dmax_out = HUGE;
            tok_out = NULL;
            expect = ESLEMAX_;             /* observed via errno */
        }
        break;

    case C_OVERLAP:
        /* Only invoked for APIs whose applicable[] marks overlap.  Place a
         * short NUL-terminated source inside dest so the overlap check sees a
         * defined extent: memcpy/strcpy/strncpy must all refuse with ESOVRLP. */
        if (cap < 32) { cap = 32; }
        dm = (rsize_t)cap;
        reset_buffers(cap, slen, 1);
        {
            size_t off = 1 + (O % 8);          /* 1..8 */
            /* source length must exceed off so the copy loop reaches the
             * overlap check before it reaches the source's terminating NUL. */
            size_t nn = off + 4 + (L % 5);     /* > off, <= 16 */
            size_t k;
            for (k = 0; k < nn; k++) dest[off + k] = (unsigned char)('a' + (int)(k % 26));
            dest[off + nn] = 0;                /* src string ends inside dest */
            n = (rsize_t)nn;
            sp = dest + off;
        }
        expect = ESOVRLP_;
        break;

    case C_UNTERMINATED:
        /* strcat_s / strncat_s: no NUL in dest within dmax. */
        dm = (rsize_t)cap;
        reset_buffers(cap, slen, 1);
        fill(dest, cap, 0x41);             /* no NUL in [0,cap) */
        n = (rsize_t)(1 + (L % cap));
        expect = ESUNTERM_;
        break;

    default:
        return 0;
    }

    errno = 0;
    /* ---------------- perform the call ---------------- */
    if (fam == 0) {
        switch (api) {
        case A_MEMCPY:   rc = cplus_sc_memcpy_s((void *)dp, dm, (const void *)sp, n); break;
        case A_MEMMOVE:  rc = cplus_sc_memmove_s((void *)dp, dm, (const void *)sp, n); break;
        case A_MEMSET:   rc = cplus_sc_memset_s((void *)dp, dm, 0x7f, n); break;
        case A_MEMCMP:   rc = cplus_sc_memcmp_s((const void *)dp, dm, (const void *)sp, n, &diff_out); break;
        case A_MEMZERO:  rc = cplus_sc_memzero_s((void *)dp, dm); break;
        case A_STRCPY:   rc = cplus_sc_strcpy_s((char *)dp, dm, (const char *)sp); break;
        case A_STRNCPY:  rc = cplus_sc_strncpy_s((char *)dp, dm, (const char *)sp, n); break;
        case A_STRCAT:   rc = cplus_sc_strcat_s((char *)dp, dm, (const char *)sp); break;
        case A_STRNCAT:  rc = cplus_sc_strncat_s((char *)dp, dm, (const char *)sp, n); break;
        case A_STRNLEN:  rc = (long long)cplus_sc_strnlen_s((const char *)dp, dm); break;
        case A_STRCMP:   rc = cplus_sc_strcmp_s((const char *)dp, dm, (const char *)sp, &ind_out); break;
        case A_STRERROR: rc = cplus_sc_strerror_s((char *)dp, dm, 1); break;
        case A_STRTOK:   rc = cplus_sc_strtok_s((char *)dp, &dmax_out, " ", &tok_out) ? 0 : (long long)errno; break;
        default: return 0;
        }
    } else {
        switch (api) {
        case A_MEMCPY:   rc = cplus_memcpy_s((void *)dp, dm, (const void *)sp, n); break;
        case A_MEMMOVE:  rc = cplus_memmove_s((void *)dp, dm, (const void *)sp, n); break;
        case A_MEMSET:   rc = cplus_memset_s((void *)dp, dm, 0x7f, n); break;
        case A_MEMCMP:   rc = cplus_memcmp_s((const void *)dp, dm, (const void *)sp, n, &diff_out); break;
        case A_MEMZERO:  rc = cplus_memzero_s((void *)dp, dm); break;
        case A_STRCPY:   rc = cplus_strcpy_s((char *)dp, dm, (const char *)sp); break;
        case A_STRNCPY:  rc = cplus_strncpy_s((char *)dp, dm, (const char *)sp, n); break;
        case A_STRCAT:   rc = cplus_strcat_s((char *)dp, dm, (const char *)sp); break;
        case A_STRNCAT:  rc = cplus_strncat_s((char *)dp, dm, (const char *)sp, n); break;
        case A_STRNLEN:  rc = (long long)cplus_strnlen_s((const char *)dp, dm); break;
        case A_STRCMP:   rc = cplus_strcmp_s((const char *)dp, dm, (const char *)sp, &ind_out); break;
        case A_STRERROR: rc = cplus_strerror_s((char *)dp, dm, 1); break;
        case A_STRTOK:   rc = cplus_strtok_s((char *)dp, &dmax_out, " ", &tok_out) ? 0 : (long long)errno; break;
        default: return 0;
        }
    }

    *rc_out = rc;
    *can_out = canary_ok(cap);
    *hostile_out = hostile;
    return (rc == (long long)expect && *can_out) ? 1 : 0;
}

int main(void) {
    int id, api, fam, cas, pass, rc_match;
    int total_refused = 0, total_canary = 0, failures = 0;
    long long rc;
    int can, hostile;
    FILE *log;

    /* Silence both families' constraint handlers: refusals are observed as
     * return codes / errno, never as aborts or diagnostic banners. */
    cplus_safe_set_constraint_mode(0);                       /* IGNORE */
    cplus_sc_set_mem_constraint_handler_s_wrapper(noop_handler);
    cplus_sc_set_str_constraint_handler_s_wrapper(noop_handler);

    log = fopen("results/scenarios.log", "w");
    if (!log) {
        fprintf(stderr, "cannot open results/scenarios.log\n");
        return 70;
    }

    for (id = 1; id <= NSCEN; id++) {
        size_t D, L, O;
        /* API round-robin over 26 variants, case round-robin over applicable
         * cases for that API.  All variable lengths come from the fixed-seed
         * PRNG -- deterministic, no wall clock. */
        api = (id - 1) % NAPI;
        fam = ((id - 1) / NAPI) % NFAM;
        {
            int k = 0, target = ((id - 1) / (NAPI * NFAM)) % NCASE;
            int chosen = C_VALID;
            int j;
            for (j = 0; j < NCASE; j++) {
                if (applicable[api][j]) {
                    if (k == target) { chosen = j; break; }
                    k++;
                }
            }
            cas = chosen;
        }
        D = rnd_range(0, 512);
        L = rnd_range(0, 512);
        O = rnd_range(0, D ? D : 1);

        rc_match = run_scenario(api, fam, cas, D, L, O, &rc, &can, &hostile);
        pass = rc_match;
        if (!pass) failures++;
        if (can) total_canary++;
        if (hostile && rc_match) total_refused++;

        printf("S %d %s_%s %s %lld %d\n",
               id, fam_name[fam], api_name[api], case_name[cas], rc, can);
        fprintf(log, "S %d %s_%s %s %lld %d\n",
                id, fam_name[fam], api_name[api], case_name[cas], rc, can);
        if (!pass) {
            fprintf(stderr, "FAIL S %d %s_%s %s rc=%lld expected-canary=%d\n",
                    id, fam_name[fam], api_name[api], case_name[cas], rc, can);
        }
    }

    fclose(log);
    printf("ADV1000 memstr %d %d %d %d\n",
           NSCEN, total_refused, total_canary, failures);
    fprintf(stderr, "ADV1000 memstr %d %d %d %d\n",
            NSCEN, total_refused, total_canary, failures);

    /* exit 70 = internal/test failure (EX_SOFTWARE convention). */
    return failures ? 70 : 0;
}
