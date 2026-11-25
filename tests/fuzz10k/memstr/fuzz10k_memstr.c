/*
 * fuzz10k_memstr.c -- 10,240 deterministic fuzz scenarios against the checked
 * memory/string APIs in build/safe/libcplus_safecrt.a.
 *
 * Two API families are exercised (both defined in that archive, verified with
 * nm at build time):
 *   cplus_sc_*_s : safeclib-backed checked APIs (BOS_UNKNOWN -> all runtime
 *                  constraint checks are active).
 *   cplus_*_s    : the C+ thin adapters that wrap the cplus_sc_*_s calls with
 *                  use-after-free rejection, logging and statistics.
 *
 * Operations (11): memcpy_s, memmove_s, memset_s, memcmp_s, memzero_s,
 *                  strcpy_s, strncpy_s, strcat_s, strncat_s, strnlen_s,
 *                  strcmp_s.
 *
 * Hostile families: null_dest, null_src, one_over (destination too small),
 * huge_n (length/size > RSIZE_MAX), overlap (undefined overlap), and
 * unterminated (unterminated destination for strcat/strncat).  A `valid`
 * family exercises normal in-bounds calls and verifies destination contents.
 *
 * CRASH / HANG DETECTION
 * ----------------------
 * Every scenario runs in a forked child.  The parent polls the child's result
 * pipe with a fixed deadline; a timeout is a HANG (child is SIGKILLed), and a
 * WIFSIGNALED wait status is a CRASH (the signal number is recorded).  A child
 * that returns a verification failure without dying is a CORRUPTION (canary
 * broken, wrong refusal code, or silent destination corruption on a valid
 * case).  The parent itself never executes an API call, so one crashing child
 * cannot kill the harness.
 *
 * DETERMINISM
 * -----------
 * A single fixed-seed xorshift64* generator drives API/family/case selection
 * and all length/offset/size parameters.  No wall clock, no ASLR-dependent
 * addresses are hashed.  The seed is recorded in NOTES.md.
 *
 * HONESTY
 * -------
 * 10,240 passing scenarios are EVIDENCE of stability for these specific
 * deterministic inputs, NOT a proof of full safety.  These APIs perform
 * runtime constraint checks (null/bounds/overlap/max-size); they do not prove
 * pointer provenance, ownership, lifetime or alias/UAF freedom, which remain
 * the caller's and the compiler's responsibility.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_runtime.h"

typedef int errno_t;
typedef size_t rsize_t;

/* ------------------------------------------------------------------ */
/* API declarations (symbols verified with nm by run.sh).              */
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

extern void cplus_sc_set_mem_constraint_handler_s_wrapper(void (*)(const char *, void *, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*)(const char *, void *, int));

/* ------------------------------------------------------------------ */
/* Configuration / constants.                                          */
/* ------------------------------------------------------------------ */
enum {
    NAPI = 11,
    NFAM = 2,
    NCASE = 7,
    NSCEN = 10240                 /* > 10,000 */
};
enum {
    A_MEMCPY, A_MEMMOVE, A_MEMSET, A_MEMCMP, A_MEMZERO,
    A_STRCPY, A_STRNCPY, A_STRCAT, A_STRNCAT, A_STRNLEN, A_STRCMP
};
enum {
    C_VALID, C_NULL_DEST, C_NULL_SRC, C_ONE_OVER, C_HUGE_N,
    C_OVERLAP, C_UNTERMINATED
};

#define MAXLEN      1024u
#define GUARD       32u
#define BSIZE       (MAXLEN + GUARD + 8u)
#define CAN         0xC3u
#define HUGE        ((rsize_t)-1)
#define CHILD_TIMEOUT_MS 5000

/* Documented safeclib refusal codes (safe_lib_errno.h). */
#define ESNULLP_  400
#define ESLEMIN_  402
#define ESLEMAX_  403
#define ESOVRLP_  404
#define ESNOSPC_  406
#define ESUNTERM_ 407

/* Reason codes carried from child to parent. */
enum { R_NONE = 0, R_CANARY = 1, R_CONTENT = 2, R_RC = 3, R_INCOMPLETE = 4 };

static const char *api_name[NAPI] = {
    "memcpy_s", "memmove_s", "memset_s", "memcmp_s", "memzero_s",
    "strcpy_s", "strncpy_s", "strcat_s", "strncat_s", "strnlen_s", "strcmp_s"
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
    /* memset_s  */ { 1,1,0,1,1,0,0 },  /* no src argument */
    /* memcmp_s  */ { 1,1,1,1,1,0,0 },  /* read-only compare */
    /* memzero_s */ { 1,1,0,0,1,0,0 },  /* no length argument */
    /* strcpy_s  */ { 1,1,1,1,1,1,0 },
    /* strncpy_s */ { 1,1,1,1,1,1,0 },
    /* strcat_s  */ { 1,1,1,1,1,0,1 },
    /* strncat_s */ { 1,1,1,1,1,0,1 },
    /* strnlen_s */ { 1,1,0,0,1,0,0 },  /* bounded count query (enum index 9) */
    /* strcmp_s  */ { 1,1,1,0,1,0,0 }   /* bounded compare (enum index 10) */
};

/* ------------------------------------------------------------------ */
/* Fixed-seed PRNG: xorshift64*.                                       */
/* ------------------------------------------------------------------ */
#define SEED UINT64_C(0xF00D5EEDCAFEBABE)
static uint64_t g_rng = SEED;
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

/* ------------------------------------------------------------------ */
/* Guarded buffers and helpers.                                        */
/* ------------------------------------------------------------------ */
static unsigned char dest[BSIZE], src[BSIZE], snap[BSIZE];

static void fill(unsigned char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = v;
}
static int bytes_eq(const unsigned char *a, const unsigned char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static int canary_ok(size_t cap) {
    size_t i;
    if (cap > MAXLEN) return 0;
    for (i = cap; i < cap + GUARD; ++i) if (dest[i] != CAN) return 0;
    return 1;
}
static void reset_buffers(size_t cap, size_t slen) {
    /* Source is a flat 0x41 fill; each case places its own NUL explicitly so
     * there is never a stray earlier terminator.  Destination starts as all
     * canary bytes inside its capacity as well; cases overwrite [0,cap) as
     * needed before taking a snapshot. */
    fill(dest, sizeof dest, CAN);
    fill(src, sizeof src, 0x41u);
    (void)cap; (void)slen;
}
static void noop_handler(const char *m, void *p, int e) {
    (void)m; (void)p; (void)e;
}

/* ------------------------------------------------------------------ */
/* Result record passed from child to parent over a pipe.              */
/* ------------------------------------------------------------------ */
typedef struct {
    int id;
    int api;
    int fam;
    int cas;
    long long rc;
    int can_ok;
    int hostile;
    int refusal_ok;
    int pass;
    int reason;
} result_t;

/* ------------------------------------------------------------------ */
/* Execute one scenario inside the (already forked) child.             */
/* Fills *r; never writes to the shared log.                           */
/* ------------------------------------------------------------------ */
static void run_scenario(int id, int api, int fam, int cas,
                         size_t D, size_t L, size_t O, result_t *r) {
    rsize_t dm, n = 0;
    size_t cap, slen, i;
    unsigned char *dp;
    const unsigned char *sp;
    int expect = 0, hostile, content_ok = 1;
    long long rc = 0;
    int out_i = 0;                      /* receives diff / indicator */
    int is_nlen = (api == A_STRNLEN);

    memset(r, 0, sizeof *r);
    r->id = id; r->api = api; r->fam = fam; r->cas = cas;
    hostile = (cas != C_VALID);
    r->hostile = hostile;

    cap = D ? D : 1;
    dm = (rsize_t)cap;
    slen = L % (cap + 1);
    dp = dest;
    sp = src;

    switch (cas) {
    case C_VALID:
        /* Keep small capacities well-formed so the expected result is
         * unambiguous; all sizes remain within the declared 0..1024 range. */
        if (api >= A_STRCPY) { if (cap < 8) cap = 8; }
        else                 { if (cap < 1) cap = 1; }
        dm = (rsize_t)cap;

        if (api == A_MEMCPY || api == A_MEMMOVE || api == A_MEMCMP || api == A_MEMSET) {
            n = (rsize_t)(L % (cap + 1));
            reset_buffers(cap, slen);
        } else if (api == A_STRCPY) {
            slen = L % (cap - 1);
            reset_buffers(cap, slen);
            src[slen] = 0;
        } else if (api == A_STRNCPY) {
            n = (rsize_t)(1 + L % (cap - 1));
            slen = L % ((size_t)n + 1);
            reset_buffers(cap, slen);
            src[slen] = 0;
        } else if (api == A_STRCAT || api == A_STRNCAT) {
            size_t dlen = L % 4;
            size_t room = cap - dlen - 1;
            reset_buffers(cap, slen);
            for (i = 0; i < dlen; ++i) dest[i] = (unsigned char)'x';
            dest[dlen] = 0;
            if (api == A_STRCAT) {
                slen = L % (room + 1);
                src[slen] = 0;
            } else {
                slen = room ? (1 + L % room) : 0;
                src[slen] = 0;
                n = (rsize_t)slen;
            }
        } else if (api == A_STRCMP) {
            slen = L % (cap - 1);
            reset_buffers(cap, slen);
            for (i = 0; i < slen; ++i) {
                dest[i] = (unsigned char)('a' + (int)(i % 26));
                src[i]  = dest[i];
            }
            dest[slen] = 0;
            src[slen] = 0;
        } else if (api == A_MEMZERO) {
            reset_buffers(cap, slen);        /* whole [0,cap) is zeroed by the API */
        } else { /* A_STRNLEN */
            slen = L % (cap - 1);
            reset_buffers(cap, slen);
            for (i = 0; i < slen; ++i) dest[i] = (unsigned char)('a' + (int)(i % 26));
            dest[slen] = 0;
            expect = (int)slen;
        }
        memcpy(snap, dest, sizeof snap);
        break;

    case C_NULL_DEST:
        dm = (rsize_t)cap;
        n = (rsize_t)((L % cap) + 1);   /* nonzero: bypass zero-length shortcut */
        reset_buffers(cap, slen);
        dp = NULL;
        expect = ESNULLP_;
        if (is_nlen) expect = 0;
        break;

    case C_NULL_SRC:
        dm = (rsize_t)cap;
        n = (rsize_t)((L % cap) + 1);
        reset_buffers(cap, slen);
        sp = NULL;
        expect = ESNULLP_;
        break;

    case C_ONE_OVER:
        dm = (rsize_t)cap;
        reset_buffers(cap, slen);
        n = (rsize_t)(cap + 1 + (L % 63));
        expect = ESNOSPC_;
        if (api == A_STRCPY) {
            fill(src, cap, 0x41);
            src[cap] = 0;
        } else if (api == A_STRNCPY) {
            fill(src, cap + 1, 0x41);
            src[cap + 1] = 0;
            n = (rsize_t)(cap + 1);
        } else if (api == A_STRCAT || api == A_STRNCAT) {
            dest[0] = 0;
            fill(src, cap, 0x41);
            src[cap] = 0;
            if (api == A_STRNCAT) n = (rsize_t)(cap + 1);
        }
        break;

    case C_HUGE_N:
        reset_buffers(cap, slen);
        expect = ESLEMAX_;
        if (api == A_MEMZERO) {
            dm = HUGE;
        } else if (api == A_MEMCPY || api == A_MEMMOVE || api == A_MEMCMP ||
                   api == A_MEMSET || api == A_STRNCPY || api == A_STRNCAT) {
            n = HUGE;
        } else {
            dm = HUGE;   /* strcpy/strcat/strcmp use dmax as the bound */
        }
        if (is_nlen) { dm = HUGE; expect = 0; }
        break;

    case C_OVERLAP:
        /* All APIs reaching this case forbid overlap (memcpy family). */
        if (cap < 32) cap = 32;
        dm = (rsize_t)cap;
        reset_buffers(cap, slen);
        {
            size_t off = 1 + (O % 8);          /* 1..8 */
            size_t nn = off + 4 + (L % 5);     /* > off, small */
            size_t k;
            for (k = 0; k < nn; ++k) dest[off + k] = (unsigned char)('a' + (int)(k % 26));
            dest[off + nn] = 0;
            n = (rsize_t)nn;
            sp = dest + off;                   /* source inside destination */
        }
        expect = ESOVRLP_;
        break;

    case C_UNTERMINATED:
        dm = (rsize_t)cap;
        reset_buffers(cap, slen);
        fill(dest, cap, 0x41);                 /* no NUL in [0,cap) */
        n = (rsize_t)(1 + (L % cap));
        expect = ESUNTERM_;
        break;

    default:
        return;
    }

    errno = 0;
    if (fam == 0) {
        switch (api) {
        case A_MEMCPY:   rc = cplus_sc_memcpy_s((void *)dp, dm, (const void *)sp, n); break;
        case A_MEMMOVE:  rc = cplus_sc_memmove_s((void *)dp, dm, (const void *)sp, n); break;
        case A_MEMSET:   rc = cplus_sc_memset_s((void *)dp, dm, 0x7f, n); break;
        case A_MEMCMP:   rc = cplus_sc_memcmp_s((const void *)dp, dm, (const void *)sp, n, &out_i); break;
        case A_MEMZERO:  rc = cplus_sc_memzero_s((void *)dp, dm); break;
        case A_STRCPY:   rc = cplus_sc_strcpy_s((char *)dp, dm, (const char *)sp); break;
        case A_STRNCPY:  rc = cplus_sc_strncpy_s((char *)dp, dm, (const char *)sp, n); break;
        case A_STRCAT:   rc = cplus_sc_strcat_s((char *)dp, dm, (const char *)sp); break;
        case A_STRNCAT:  rc = cplus_sc_strncat_s((char *)dp, dm, (const char *)sp, n); break;
        case A_STRNLEN:  rc = (long long)cplus_sc_strnlen_s((const char *)dp, dm); break;
        case A_STRCMP:   rc = cplus_sc_strcmp_s((const char *)dp, dm, (const char *)sp, &out_i); break;
        default: return;
        }
    } else {
        switch (api) {
        case A_MEMCPY:   rc = cplus_memcpy_s((void *)dp, dm, (const void *)sp, n); break;
        case A_MEMMOVE:  rc = cplus_memmove_s((void *)dp, dm, (const void *)sp, n); break;
        case A_MEMSET:   rc = cplus_memset_s((void *)dp, dm, 0x7f, n); break;
        case A_MEMCMP:   rc = cplus_memcmp_s((const void *)dp, dm, (const void *)sp, n, &out_i); break;
        case A_MEMZERO:  rc = cplus_memzero_s((void *)dp, dm); break;
        case A_STRCPY:   rc = cplus_strcpy_s((char *)dp, dm, (const char *)sp); break;
        case A_STRNCPY:  rc = cplus_strncpy_s((char *)dp, dm, (const char *)sp, n); break;
        case A_STRCAT:   rc = cplus_strcat_s((char *)dp, dm, (const char *)sp); break;
        case A_STRNCAT:  rc = cplus_strncat_s((char *)dp, dm, (const char *)sp, n); break;
        case A_STRNLEN:  rc = (long long)cplus_strnlen_s((const char *)dp, dm); break;
        case A_STRCMP:   rc = cplus_strcmp_s((const char *)dp, dm, (const char *)sp, &out_i); break;
        default: return;
        }
    }

    /* -------- valid-case content verification (no silent corruption) -------- */
    if (cas == C_VALID) {
        switch (api) {
        case A_MEMCPY:
        case A_MEMMOVE:
            if (!bytes_eq(dest, src, (size_t)n)) content_ok = 0;
            if (!bytes_eq(dest + (size_t)n, snap + (size_t)n, cap - (size_t)n)) content_ok = 0;
            break;
        case A_MEMSET:
            for (i = 0; i < (size_t)n; ++i) if (dest[i] != 0x7fu) { content_ok = 0; break; }
            if (!bytes_eq(dest + (size_t)n, snap + (size_t)n, cap - (size_t)n)) content_ok = 0;
            break;
        case A_MEMCMP:
            if (!bytes_eq(dest, snap, cap)) content_ok = 0;   /* read-only op */
            if (rc != 0) content_ok = 0;
            break;
        case A_MEMZERO:
            for (i = 0; i < cap; ++i) if (dest[i] != 0u) { content_ok = 0; break; }
            break;
        case A_STRCPY:
            if (!bytes_eq(dest, src, slen) || dest[slen] != 0) content_ok = 0;
            break;
        case A_STRNCPY:
            if (!bytes_eq(dest, src, slen) || dest[slen] != 0) content_ok = 0;
            break;
        case A_STRCAT: {
            size_t dlen = 0;
            while (dlen < cap && snap[dlen] != 0) ++dlen;
            if (!bytes_eq(dest, snap, dlen)) content_ok = 0;
            if (!bytes_eq(dest + dlen, src, slen)) content_ok = 0;
            if (dest[dlen + slen] != 0) content_ok = 0;
            break;
        }
        case A_STRNCAT: {
            size_t dlen = 0;
            while (dlen < cap && snap[dlen] != 0) ++dlen;
            if (!bytes_eq(dest, snap, dlen)) content_ok = 0;
            if (!bytes_eq(dest + dlen, src, slen)) content_ok = 0;
            if (dest[dlen + slen] != 0) content_ok = 0;
            break;
        }
        case A_STRNLEN:
            if ((size_t)rc != slen) content_ok = 0;
            break;
        case A_STRCMP:
            if (!bytes_eq(dest, snap, cap)) content_ok = 0;
            if (rc != 0) content_ok = 0;
            break;
        default:
            break;
        }
    }

    r->rc = rc;
    r->can_ok = canary_ok(cap);
    if (hostile) {
        r->refusal_ok = (rc == (long long)expect) && r->can_ok;
        if (!r->refusal_ok) r->reason = r->can_ok ? R_RC : R_CANARY;
    } else {
        if (!r->can_ok) r->reason = R_CANARY;
        else if (!content_ok) r->reason = R_CONTENT;
        else if (rc != (long long)expect) r->reason = R_RC;
    }
    r->pass = (r->reason == R_NONE) && (!hostile || r->refusal_ok);
}

/* ------------------------------------------------------------------ */
/* Parent-side counters and reporting.                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    long scenarios, crashes, hangs, corruptions, refusals;
} counts_t;

int main(void) {
    counts_t c = {0, 0, 0, 0, 0};
    long api_seen[NAPI] = {0}, fam_seen[NFAM] = {0}, case_seen[NCASE] = {0};
    FILE *log;
    int id;

    setvbuf(stdout, NULL, _IONBF, 0);

    /* Silence refusals: observe return codes, not aborts or banners. */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_sc_set_mem_constraint_handler_s_wrapper(noop_handler);
    cplus_sc_set_str_constraint_handler_s_wrapper(noop_handler);
    cplus_runtime_set_violation_trap(0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);

    log = fopen("results/scenarios.log", "w");
    if (!log) {
        fprintf(stderr, "fuzz10k: cannot open results/scenarios.log\n");
        return 70;
    }

    for (id = 1; id <= NSCEN; ++id) {
        int api, fam, cas, k, j, target;
        size_t D, L, O;
        int pfd[2];
        pid_t pid;
        struct pollfd pfdp;
        int pr, got = 0, timed_out = 0;
        result_t r;
        int status = 0;
        unsigned char *buf = (unsigned char *)&r;

        /* Random (but deterministic) operation / family / case. */
        api = (int)(rnd64() % NAPI);
        fam = (int)(rnd64() % NFAM);
        target = (int)(rnd64() % NCASE);
        k = 0; cas = C_VALID;
        for (j = 0; j < NCASE; ++j) {
            if (applicable[api][j]) {
                if (k == target) { cas = j; break; }
                ++k;
            }
        }
        D = rnd_range(0, MAXLEN);
        L = rnd_range(0, MAXLEN);
        O = rnd_range(0, D ? D : 1);

        api_seen[api]++; fam_seen[fam]++; case_seen[cas]++;

        if (pipe(pfd) != 0) {
            fprintf(stderr, "fuzz10k: pipe failed at id=%d\n", id);
            fclose(log);
            return 70;
        }
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "fuzz10k: fork failed at id=%d\n", id);
            close(pfd[0]); close(pfd[1]);
            fclose(log);
            return 70;
        }
        if (pid == 0) {
            /* -------- child -------- */
            close(pfd[0]);
            run_scenario(id, api, fam, cas, D, L, O, &r);
            {
                ssize_t w = write(pfd[1], &r, sizeof r);
                (void)w;
            }
            close(pfd[1]);
            _exit(r.pass ? 0 : 70);
        }

        /* -------- parent -------- */
        close(pfd[1]);
        pfdp.fd = pfd[0];
        pfdp.events = POLLIN;
        pfdp.revents = 0;
        pr = poll(&pfdp, 1, CHILD_TIMEOUT_MS);
        if (pr == 0) {
            timed_out = 1;
        } else if (pr > 0) {
            while (got < (int)sizeof r) {
                ssize_t nread = read(pfd[0], buf + got, sizeof r - (size_t)got);
                if (nread > 0) { got += (int)nread; continue; }
                if (nread == 0) break;      /* EOF */
                if (errno == EINTR) continue;
                break;
            }
        }
        close(pfd[0]);
        if (timed_out) kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { /* retry */ }

        c.scenarios++;
        if (timed_out) {
            c.hangs++;
            fprintf(log, "S %d %s_%s %s HANG\n",
                    id, fam_name[fam], api_name[api], case_name[cas]);
            continue;
        }
        if (WIFSIGNALED(status)) {
            c.crashes++;
            fprintf(log, "S %d %s_%s %s CRASH sig=%d\n",
                    id, fam_name[fam], api_name[api], case_name[cas],
                    WTERMSIG(status));
            continue;
        }
        if (got != (int)sizeof r) {
            /* Exited normally without a result record: harness anomaly. */
            c.corruptions++;
            fprintf(log, "S %d %s_%s %s INCOMPLETE exit=%d\n",
                    id, fam_name[fam], api_name[api], case_name[cas],
                    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            continue;
        }

        fprintf(log, "S %d %s_%s %s %lld %d %d %d\n",
                id, fam_name[fam], api_name[api], case_name[cas],
                r.rc, r.can_ok, r.pass, r.reason);
        if (r.hostile && r.refusal_ok) c.refusals++;
        if (!r.pass) c.corruptions++;
    }

    fclose(log);

    /* Coverage sanity: every variant and every case family must appear. */
    {
        int i, ok = 1;
        for (i = 0; i < NAPI; ++i) if (api_seen[i] == 0) ok = 0;
        for (i = 0; i < NFAM; ++i) if (fam_seen[i] == 0) ok = 0;
        for (i = 0; i < NCASE; ++i) if (case_seen[i] == 0) ok = 0;
        if (!ok) {
            fprintf(stderr, "fuzz10k: coverage gap (harness bug)\n");
            c.corruptions++;   /* treat as harness failure */
        }
    }

    printf("FUZZ10K memstr %ld %ld %ld %ld %ld\n",
           c.scenarios, c.crashes, c.hangs, c.corruptions, c.refusals);
    fprintf(stderr, "FUZZ10K memstr %ld %ld %ld %ld %ld\n",
            c.scenarios, c.crashes, c.hangs, c.corruptions, c.refusals);

    return (c.crashes || c.hangs || c.corruptions) ? 70 : 0;
}
