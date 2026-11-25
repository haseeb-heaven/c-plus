/*
 * fuzz_format.c -- deterministic 10k-scenario fuzz of the checked
 * format/conversion APIs in libcplus_safecrt.a + libcplus_hardened.a.
 *
 * Discipline (mirrors tests/crt/fuzz/f100_fmt_common.h):
 *   - A fixed-seed xorshift64* PRNG (no time, no external input) selects every
 *     scenario parameter.  Re-running with the same seed yields byte-identical
 *     scenarios.log (apart from the pid-free content, there is no pid in it).
 *   - No fuzzer byte is ever used directly as a format string.  Format strings
 *     are *composed* only from the fixed, type-matched token tables below, so
 *     every generated vararg list is C-type-correct for its generated format.
 *   - Every destination is surrounded by canaries; writes past dmax, missing
 *     NUL termination and modified %n sinks are detected as corruptions.
 *
 * Verdicts: OK (rc>=0 && not truncated), TRUNC (rc>=dmax), REFUSED (rc<0).
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cplus_libc.h"   /* cplus_strtol / cplus_strtoul / cplus_strtoimax */
#include "safe_format.h"
#include "safe_log.h"
#include "safe_runtime.h"

/* ------------------------------------------------------------------ config */
#define SEED          UINT64_C(0x46555A5A3130304B) /* ASCII "FUZZ100K" */
#ifndef NSCEN
#define NSCEN         10000
#endif
#define BUFCAP        520    /* > largest dmax (512) */
#define CANARY        0xA5u
#define NARGS         6      /* fixed vararg arity per scenario */
#define WATCHDOG_SEC  540    /* child self-limit; run.sh uses timeout 600 */
#define HSIZE         16384  /* distinct-scenario hash table (power of two) */

/* ------------------------------------------------------------------- PRNG */
static uint64_t prng_state = SEED;
static uint64_t nx(void) {
    uint64_t x = prng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    prng_state = x;
    return x * UINT64_C(2685821657736338717);
}
static uint32_t below(uint32_t n) { return n ? (uint32_t)(nx() % (uint64_t)n) : 0u; }

/* -------------------------------------------------------------- token sets */
typedef struct { const char *const *t; unsigned n; } tokset;
#define TS(a) { (a), (unsigned)(sizeof(a) / sizeof((a)[0])) }

/* Each token consumes exactly one vararg of the group's C type. */
static const char *const T_INT[]  = { "%d", "%i", "%+d", "% d", "%08d", "%-10d", "%.6d", "%+08d" };
static const char *const T_UINT[] = { "%u", "%x", "%X", "%o", "%08x", "%#x", "%#o", "%.5x" };
static const char *const T_LL[]   = { "%lld", "%+lld", "%016lld", "%-14lld", "%.8lld", "%lld" };
static const char *const T_ULL[]  = { "%llu", "%llx", "%#llx", "%020llu", "%#018llX", "%llu" };
static const char *const T_SIZE[] = { "%zu", "%10zu", "%zx", "%#zx", "%.6zu", "%zu" };
static const char *const T_STR[]  = { "%s", "%.8s", "%-12s", "%-16.6s", "%20.10s", "%s" };
static const char *const T_N[]    = { "%n", "a%n", "%n%n", "v=%n", "[%n]", "%n-%n" };

static const char *const S_INT[] = { "%d", "%i", "%3d", "%*d", "%05d", "%d" };
static const char *const S_UINT[] = { "%u", "%x", "%o", "%*u", "%4x", "%u" };
static const char *const S_LL[] = { "%lld", "%4lld", "%*lld", "%2lld", "%lld" };
static const char *const S_ULL[] = { "%llu", "%4llu", "%*llu", "%llu" };
/* Includes intentionally unsafe unbounded conversions: the API must REFUSE. */
static const char *const S_STR[] = { "%16s", "%8s", "%*16s", "%32s", "%s", "%[a-z]", "%4s" };
static const char *const S_N[] = { "%n", "a%n", "%n%n", "v=%n", "%n" };

static const char *const LIT[] = { "", "-", ":", "abc", " ", "!", "007", ".", "/" };

enum { G_INT, G_UINT, G_LL, G_ULL, G_SIZE, G_STR, G_N, NGROUP };
static const tokset PRINT_TOKENS[NGROUP] = {
    TS(T_INT), TS(T_UINT), TS(T_LL), TS(T_ULL), TS(T_SIZE), TS(T_STR), TS(T_N)
};

enum { S_INT_G, S_UINT_G, S_LL_G, S_ULL_G, S_STR_G, S_N_G, NSCANGROUP };
static const tokset SCAN_TOKENS[NSCANGROUP] = {
    TS(S_INT), TS(S_UINT), TS(S_LL), TS(S_ULL), TS(S_STR), TS(S_N)
};
static const tokset LITERALS = TS(LIT);

/* ------------------------------------------------------- guarded storage */
typedef struct { unsigned char pre[16]; char buf[BUFCAP]; unsigned char post[16]; } bbuf;
typedef struct { unsigned char pre[16]; int c[NARGS]; unsigned char post[16]; } nguard;
typedef struct {
    unsigned char pre[16];
    int i[NARGS];
    unsigned u[NARGS];
    long long ll[NARGS];
    unsigned long long ull[NARGS];
    char s[NARGS][64];
    unsigned char post[16];
} scan_scratch;

typedef struct {
    int i[NARGS]; unsigned u[NARGS]; long long ll[NARGS];
    unsigned long long ull[NARGS]; size_t sz[NARGS];
    char *s[NARGS]; int *np[NARGS];
} vals;

static int all_bytes(const unsigned char *p, size_t n, unsigned char c) {
    size_t i;
    for (i = 0; i < n; ++i) if (p[i] != c) return 0;
    return 1;
}
static void bbuf_init(bbuf *b) { memset(b, CANARY, sizeof *b); }
static int bbuf_ok(const bbuf *b, size_t dmax) {
    size_t i;
    if (!all_bytes(b->pre, sizeof b->pre, CANARY) ||
        !all_bytes(b->post, sizeof b->post, CANARY)) return 0;
    /* dmax <= 512 < BUFCAP: everything from dmax onward must stay canary. */
    for (i = dmax; i < BUFCAP; ++i)
        if ((unsigned char)b->buf[i] != CANARY) return 0;
    return 1;
}
static void nguard_init(nguard *g) {
    int k;
    memset(g, CANARY, sizeof *g);
    for (k = 0; k < NARGS; ++k) g->c[k] = 0x5A5A5A5A;
}
static int nguard_ok(const nguard *g) {
    int k;
    if (!all_bytes(g->pre, sizeof g->pre, CANARY) ||
        !all_bytes(g->post, sizeof g->post, CANARY)) return 0;
    for (k = 0; k < NARGS; ++k) if (g->c[k] != 0x5A5A5A5A) return 0;
    return 1;
}
static void scan_init(scan_scratch *s) {
    int k;
    memset(s, CANARY, sizeof *s);
    for (k = 0; k < NARGS; ++k) s->i[k] = 0x5A5A5A5A; /* %n sink sentinel */
}
static int scan_ok(const scan_scratch *s) {
    if (!all_bytes(s->pre, sizeof s->pre, CANARY) ||
        !all_bytes(s->post, sizeof s->post, CANARY)) return 0;
    return 1; /* per-string tails are checked in the scenario body */
}

/* -------------------------------------------------------------- generators */
static void build_fmt(char *out, size_t cap, const tokset *ts, unsigned ntok) {
    size_t len = 0;
    unsigned k;
    out[0] = '\0';
    for (k = 0; k < ntok; ++k) {
        const tokset *pick = (below(4) == 0) ? &LITERALS : ts;
        const char *tok = pick->t[below(pick->n)];
        size_t tl = strlen(tok);
        if (len + tl + 1 > cap) break;
        memcpy(out + len, tok, tl);
        len += tl;
        out[len] = '\0';
    }
}

static const char *const NUM_PATS[] = {
    "0", "1", "-1", "+42", "  -123abc", "0x1F", "0777", "0b101",
    "9223372036854775807", "9223372036854775808", "-9223372036854775809",
    "18446744073709551615", "99999999999999999999999999999999",
    "deadBEEF", "   +000123", "junk", "", "-", "+-1", "42 99 -7"
};
#define NNUM ((unsigned)(sizeof NUM_PATS / sizeof NUM_PATS[0]))

static void make_input(char *out, size_t cap, unsigned olen) {
    const char *p = NUM_PATS[below(NNUM)];
    size_t len = strlen(p);
    if (len > cap - 1) len = cap - 1;
    memcpy(out, p, len);
    while (len < olen && len + 1 < cap) out[len++] = ' ';
    out[len] = '\0';
}
static void make_strarg(char *out, size_t cap, unsigned olen) {
    static const char alpha[] = "abcdef0123456789_-";
    size_t n = olen, i;
    if (n > cap - 1) n = cap - 1;
    for (i = 0; i < n; ++i) out[i] = alpha[below((unsigned)(sizeof alpha - 1))];
    out[n] = '\0';
}

/* ------------------------------------------------------------- hash set */
static uint64_t seen[HSIZE];
static int is_duplicate(uint64_t fp) {
    size_t h = (size_t)(fp & (HSIZE - 1));
    size_t probe;
    for (probe = 0; probe < HSIZE; ++probe) {
        size_t k = (h + probe) & (HSIZE - 1);
        if (seen[k] == fp) return 1;
        if (seen[k] == 0) { seen[k] = fp; return 0; }
    }
    return 1;
}
static uint64_t fingerprint(int api, const char *fmt, size_t dmax, unsigned olen,
                            int g, int base, unsigned flags, const char *input) {
    uint64_t h = UINT64_C(1469598103934665603); /* FNV-1a 64 */
    const unsigned char *p;
    uint64_t scalars[4];
    size_t i;
#define MIXBYTE(b) do { h ^= (uint64_t)(unsigned char)(b); h *= UINT64_C(1099511628211); } while (0)
    MIXBYTE(api); MIXBYTE(g); MIXBYTE(base); MIXBYTE(flags);
    scalars[0] = dmax; scalars[1] = olen;
    for (i = 0; i < 2; ++i) {
        p = (const unsigned char *)&scalars[i];
        for (size_t j = 0; j < sizeof(uint64_t); ++j) MIXBYTE(p[j]);
    }
    for (p = (const unsigned char *)fmt; *p; ++p) MIXBYTE(*p);
    for (p = (const unsigned char *)input; *p; ++p) MIXBYTE(*p);
#undef MIXBYTE
    return h ? h : 1;
}

/* ------------------------------------------------------------ API dispatch */
enum { A_SN, A_SP, A_PF, A_FP, A_SS, A_FS, A_L, A_UL, A_IM, NAPI };
static const char *const API_NAME[NAPI] = {
    "snprintf_s", "sprintf_s", "printf_s", "fprintf_s",
    "sscanf_s", "fscanf_s", "strtol", "strtoul", "strtoimax"
};

/* UNPAREN lets a parenthesised comma list be passed as one macro argument. */
#define UNPAREN(x) UNPAREN_I x
#define UNPAREN_I(...) __VA_ARGS__
#define PRINT_GROUP(FN, PRE)                                                        \
    switch (g) {                                                                    \
    case G_INT:  return FN(UNPAREN(PRE) fmt, v->i[0],v->i[1],v->i[2],v->i[3],v->i[4],v->i[5]);      \
    case G_UINT: return FN(UNPAREN(PRE) fmt, v->u[0],v->u[1],v->u[2],v->u[3],v->u[4],v->u[5]);      \
    case G_LL:   return FN(UNPAREN(PRE) fmt, v->ll[0],v->ll[1],v->ll[2],v->ll[3],v->ll[4],v->ll[5]); \
    case G_ULL:  return FN(UNPAREN(PRE) fmt, v->ull[0],v->ull[1],v->ull[2],v->ull[3],v->ull[4],v->ull[5]); \
    case G_SIZE: return FN(UNPAREN(PRE) fmt, v->sz[0],v->sz[1],v->sz[2],v->sz[3],v->sz[4],v->sz[5]); \
    case G_STR:  return FN(UNPAREN(PRE) fmt, v->s[0],v->s[1],v->s[2],v->s[3],v->s[4],v->s[5]);      \
    case G_N:    return FN(UNPAREN(PRE) fmt, v->np[0],v->np[1],v->np[2],v->np[3],v->np[4],v->np[5]); \
    }                                                                               \
    return -1

static int print_call(int api, char *dest, size_t dmax, FILE *st,
                      const char *fmt, int g, const vals *v) {
    switch (api) {
    case A_SN: PRINT_GROUP(cplus_snprintf_s, (dest, dmax, ));
    case A_SP: PRINT_GROUP(cplus_sprintf_s,  (dest, dmax, ));
    case A_PF: PRINT_GROUP(cplus_printf_s,   ());
    case A_FP: PRINT_GROUP(cplus_fprintf_s,  (st, ));
    }
    return -1;
}

#define SCAN_GROUP(FN, PRE)                                                                       \
    switch (g) {                                                                                  \
    case S_INT_G:  return FN(UNPAREN(PRE) fmt, s->i,   s->i+1,   s->i+2,   s->i+3,   s->i+4,   s->i+5);   \
    case S_UINT_G: return FN(UNPAREN(PRE) fmt, s->u,   s->u+1,   s->u+2,   s->u+3,   s->u+4,   s->u+5);   \
    case S_LL_G:   return FN(UNPAREN(PRE) fmt, s->ll,  s->ll+1,  s->ll+2,  s->ll+3,  s->ll+4,  s->ll+5);  \
    case S_ULL_G:  return FN(UNPAREN(PRE) fmt, s->ull, s->ull+1, s->ull+2, s->ull+3, s->ull+4, s->ull+5); \
    case S_STR_G:  return FN(UNPAREN(PRE) fmt, s->s[0],s->s[1],s->s[2],s->s[3],s->s[4],s->s[5]);           \
    case S_N_G:    return FN(UNPAREN(PRE) fmt, &s->i[0],&s->i[1],&s->i[2],&s->i[3],&s->i[4],&s->i[5]);     \
    }                                                                                             \
    return -1

static int scan_call(int api, const char *input, FILE *st,
                     const char *fmt, int g, scan_scratch *s) {
    switch (api) {
    case A_SS: SCAN_GROUP(cplus_sscanf_s, (input, ));
    case A_FS: SCAN_GROUP(cplus_fscanf_s, (st, ));
    }
    return -1;
}

/* ------------------------------------------------------- child bookkeeping */
static volatile sig_atomic_t g_done = 0;
static volatile sig_atomic_t g_hang = 0;
static int g_log_fd = -1;
static long g_corrupt = 0;
static long g_refused = 0;

static void emit_result(int sig, int hang) {
    char msg[192];
    int n = snprintf(msg, sizeof msg,
                     "RESULT scenarios=%ld crashes=%d hangs=%d corruptions=%ld refusals=%ld signal=%d\n",
                     (long)g_done, (sig ? 1 : 0), hang, g_corrupt, g_refused, sig);
    if (g_log_fd >= 0 && n > 0) (void)write(g_log_fd, msg, (size_t)n);
}
static void fatal_signal(int sig) { emit_result(sig, 0); _exit(90); }
static void watchdog(int sig) { (void)sig; g_hang = 1; emit_result(SIGALRM, 1); _exit(91); }

static void install_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fatal_signal;
    sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGSEGV, &sa, NULL);
    (void)sigaction(SIGBUS,  &sa, NULL);
    (void)sigaction(SIGILL,  &sa, NULL);
    (void)sigaction(SIGABRT, &sa, NULL);
    (void)sigaction(SIGFPE,  &sa, NULL);
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = watchdog;
    sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGALRM, &sa, NULL);
    (void)alarm(WATCHDOG_SEC);
}

/* --------------------------------------------------------------- scenario */
static void run_one(FILE *log, unsigned idx, int api, unsigned group,
                    const char *fmt, size_t dmax, unsigned olen,
                    int base, unsigned flags, const char *input) {
    char verdict[12];
    int rc = -1;
    int corrupt = 0;
    int null_dest = (flags & 1u) != 0;
    int null_stream = (flags & 2u) != 0;
    int null_input = (flags & 4u) != 0;
    char *dest = NULL;
    FILE *st = NULL;
    bbuf b;
    nguard ng;
    scan_scratch sc;
    vals v;
    char strarg[BUFCAP];
    char *endp = NULL;

    memset(&v, 0, sizeof v);
    /* Type-matched, deterministic values; extremes occupy the first slots. */
    v.i[0] = INT_MIN; v.i[1] = INT_MAX; v.i[2] = -1; v.i[3] = 0; v.i[4] = 1; v.i[5] = (int)nx();
    v.u[0] = 0u; v.u[1] = UINT_MAX; v.u[2] = 1u; v.u[3] = 65535u; v.u[4] = 0x80000000u; v.u[5] = (unsigned)nx();
    v.ll[0] = LLONG_MIN; v.ll[1] = LLONG_MAX; v.ll[2] = -1; v.ll[3] = 0; v.ll[4] = 1; v.ll[5] = (long long)(int64_t)nx();
    v.ull[0] = 0ULL; v.ull[1] = ULLONG_MAX; v.ull[2] = 1ULL; v.ull[3] = 0x8000000000000000ULL;
    v.ull[4] = 42ULL; v.ull[5] = (unsigned long long)nx();
    v.sz[0] = 0u; v.sz[1] = SIZE_MAX; v.sz[2] = 1u; v.sz[3] = 512u; v.sz[4] = (size_t)-2; v.sz[5] = (size_t)nx();
    make_strarg(strarg, sizeof strarg, olen);
    for (int k = 0; k < NARGS; ++k) v.s[k] = strarg;

    bbuf_init(&b);
    nguard_init(&ng);
    for (int k = 0; k < NARGS; ++k) v.np[k] = &ng.c[k];
    scan_init(&sc);

    if (api <= A_FP) {
        if (api == A_FP && !null_stream) {
            st = tmpfile(); /* bounded sink; fprintf_s output must go somewhere */
        }
        if (!null_dest) dest = b.buf;
        rc = print_call(api, dest, dmax, null_stream ? NULL : st,
                        fmt, (int)group, &v);
        if (st) fclose(st);
        /* Only snprintf_s / sprintf_s write into `b`; stream APIs do not. */
        if ((api == A_SN || api == A_SP) && !null_dest) {
            if (!bbuf_ok(&b, dmax)) corrupt = 1;
            if (rc >= 0 && dmax > 0) {
                /* vsnprintf NUL-terminates at rc when not truncated, else at
                 * dmax-1.  Both are bounded writes we verify explicitly. */
                size_t nulpos = ((size_t)rc < dmax) ? (size_t)rc : dmax - 1;
                if (b.buf[nulpos] != '\0') corrupt = 1;
            }
        }
        /* A refused %n must not have touched the guarded sink. */
        if (strstr(fmt, "%n") != NULL && !nguard_ok(&ng)) corrupt = 1;
    } else if (api <= A_FS) {
        if (!null_stream) {
            st = tmpfile();
            if (st) {
                size_t il = strlen(input);
                if (il) (void)fwrite(input, 1, il, st);
                rewind(st);
            }
        }
        {
            const char *src = (api == A_SS) ? (null_input ? NULL : input) : input;
            rc = scan_call(api, src, null_stream ? NULL : st, fmt, (int)group, &sc);
        }
        if (!scan_ok(&sc)) corrupt = 1;
        for (int k = 0; k < NARGS; ++k) {
            size_t j;
            /* widest bounded %s is %32s => 32 chars + NUL at index 32 */
            for (j = 40; j < sizeof sc.s[k]; ++j)
                if ((unsigned char)sc.s[k][j] != CANARY) corrupt = 1;
        }
        if (strstr(fmt, "%n") != NULL) {
            for (int k = 0; k < NARGS; ++k)
                if (sc.i[k] != 0x5A5A5A5A) corrupt = 1;
        }
        if (st) fclose(st);
    } else {
        const char *s = null_input ? NULL : input;
        errno = 0;
        switch (api) {
        case A_L:  rc = (int)cplus_strtol(s, &endp, base); break;
        case A_UL: rc = (int)cplus_strtoul(s, &endp, base); break;
        case A_IM: rc = (int)cplus_strtoimax(s, &endp, base); break;
        }
        if (!null_input && endp != NULL) {
            if (endp < s || endp > s + strlen(s)) corrupt = 1;
        }
        if (null_input && api == A_IM && rc != 0) corrupt = 1;
    }

    if (rc < 0) { strcpy(verdict, "REFUSED"); ++g_refused; }
    else if (dmax > 0 && api <= A_SP && rc >= (int)dmax) strcpy(verdict, "TRUNC");
    else strcpy(verdict, "OK");

    if (corrupt) ++g_corrupt;

    fprintf(log, "%u|api=%s|grp=%u|dmax=%zu|olen=%u|base=%d|flags=%u|fmt=%s|input=%s|rc=%d|verdict=%s|checks=%s\n",
            idx, API_NAME[api], group, dmax, olen, base, flags, fmt, input, rc, verdict,
            corrupt ? "CORRUPT" : "ok");
    ++g_done;
}

/* -------------------------------------------------------------------- main */
int main(int argc, char **argv) {
    const char *logpath = (argc > 1) ? argv[1] : "scenarios.log";
    FILE *log;
    pid_t pid;
    int status = 0;
    long scenarios = 0, crashes = 0, hangs = 0, corruptions = 0, refusals = 0;
    unsigned i;

    log = fopen(logpath, "w");
    if (!log) { perror("fopen log"); return 2; }
    setvbuf(log, NULL, _IOLBF, 0);
    g_log_fd = fileno(log);

    /* Silent, recoverable violation path so %n / null / UAF refusals can be
     * counted instead of terminating via the default exit(70) trap. */
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_runtime_set_violation_trap(0);

    prng_state = SEED;
    pid = fork();
    if (pid < 0) { perror("fork"); fclose(log); return 2; }
    if (pid == 0) {
        char fmt[128], input[BUFCAP];
        install_handlers();
        for (i = 0; i < NSCEN; ++i) {
            int api, base, gscan, retry;
            unsigned ntok, olen, flags, g;
            size_t dmax;
            uint64_t fp;
            /* Force periodic %n coverage in addition to random selection. */
            int force_n = ((i % 97) == 0);
            for (retry = 0; retry < 16; ++retry) {
                api = (int)below(NAPI);
                olen = below(513);
                dmax = below(513);
                flags = 0;
                {
                    static const int bases2[] = { -1, 0, 2, 8, 10, 16, 36, 37 };
                    base = bases2[below((unsigned)(sizeof bases2 / sizeof bases2[0]))];
                }
                if (api <= A_FP) {
                    const tokset *ts;
                    g = force_n ? (unsigned)G_N : below(NGROUP);
                    ts = &PRINT_TOKENS[g];
                    ntok = 1u + below(6u);
                    build_fmt(fmt, sizeof fmt, ts, ntok);
                    if (below(16) == 0) flags |= 1u; /* null dest */
                } else if (api <= A_FS) {
                    const tokset *ts;
                    g = force_n ? (unsigned)S_N_G : below(NSCANGROUP);
                    gscan = (int)g;
                    ts = &SCAN_TOKENS[gscan];
                    ntok = 1u + below(6u);
                    build_fmt(fmt, sizeof fmt, ts, ntok);
                    if (below(16) == 0) flags |= 4u; /* null input */
                    if (below(16) == 0) flags |= 2u; /* null stream */
                } else {
                    g = 0;
                    ntok = 1u + below(4u);
                    build_fmt(fmt, sizeof fmt, &LITERALS, ntok);
                    if (api == A_IM && below(16) == 0) flags |= 4u; /* guarded null */
                }
                make_input(input, sizeof input, olen);
                fp = fingerprint(api, fmt, dmax, olen, (int)g, base, flags, input);
                if (!is_duplicate(fp)) break;
            }
            run_one(log, i, api, g, fmt, dmax, olen, base, flags, input);
        }
        emit_result(0, 0);
        fflush(log);
        _exit(0);
    }

    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    fclose(log);

    /* Parse the final RESULT line (written by the child or its signal handler). */
    {
        FILE *r = fopen(logpath, "r");
        char line[256], result[256];
        result[0] = '\0';
        if (r) {
            while (fgets(line, sizeof line, r))
                if (strncmp(line, "RESULT ", 7) == 0)
                    strcpy(result, line);
            fclose(r);
        }
        if (result[0]) {
            long sc = 0, cr = 0, hg = 0, co = 0, rf = 0;
            if (sscanf(result, "RESULT scenarios=%ld crashes=%ld hangs=%ld corruptions=%ld refusals=%ld",
                       &sc, &cr, &hg, &co, &rf) == 5) {
                scenarios = sc; crashes = cr; hangs = hg; corruptions = co; refusals = rf;
            }
        }
    }
    if (WIFSIGNALED(status)) { crashes = 1; }
    else if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 90) crashes = 1;
        else if (code == 91) hangs = 1;
        else if (code != 0) corruptions++;
    }
    if (g_hang) hangs = 1; /* parent copy stays 0; child encodes it in RESULT */

    /* printf_s output shares this fd (the run script redirects it to a sink);
     * force a fresh line so the summary is grep-anchored. */
    printf("\nFUZZ10K format %ld %ld %ld %ld %ld\n",
           scenarios, crashes, hangs, corruptions, refusals);
    fflush(stdout);
    return (crashes || hangs || corruptions) ? 1 : 0;
}
