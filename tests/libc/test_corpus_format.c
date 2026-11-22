/* tests/libc/test_corpus_format.c
 *
 * Deterministic adversarial corpus for the CHECKED narrow formatting API
 * (cplus_snprintf_s / cplus_sprintf_s family) plus a guarded wide-format
 * section (cplus_swprintf).
 *
 * Build (from the repository root):
 *   sh tests/libc/build_libc.sh >/dev/null
 *   sh scripts/build_safecrt.sh >/dev/null
 *   clang -std=c99 -Wall -Wextra -pedantic -O2 \
 *     -Ilibc -Ilibc/include -Istdlib/safe \
 *     -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
 *     tests/libc/test_corpus_format.c \
 *     build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
 *     -lm -pthread -o /tmp/corpus_format && /tmp/corpus_format
 *
 * What is asserted for every (format, dest_capacity, arg_count) tuple:
 *   (a) no crash / no signal,
 *   (b) no byte at/after dmax is modified (and no write anywhere when the
 *       call is rejected), verified with leading/trailing canaries,
 *   (c) the bogus %n target (a canary-guarded int, never a wild address in the
 *       main process) is unchanged; wild-address %n probes run in fork()ed
 *       children so a real write-through shows up as a FINDING,
 *   (d) a defined return: >= 0 for accepted formats, exactly -1/EINVAL for
 *       formats containing %n.
 *
 * OBSERVED %n BEHAVIOUR (verified against stdlib/safe/src/safe_format.c):
 *   cplus_snprintf_s -> cplus_vsnprintf_s -> cplus_fmt_has_percent_n() ->
 *   fmt_violation(): %n is REJECTED.  The call returns -1 with errno=EINVAL,
 *   no destination byte is touched, and the %n argument is never read or
 *   dereferenced.  It is neither ignored nor passed through to vsnprintf.
 *   The same rejection applies to cplus_sprintf_s and to the checked
 *   printf/fprintf/scanf families.  Note that the violation trap is ON by
 *   default (cplus_runtime_violation -> _exit(70)); this test disables it so
 *   the -1/EINVAL contract is observable.
 *
 * WIDE-FORMAT CAVEAT (documented, not a silent finding):
 *   cplus_swprintf is a thin platform-backed wrapper around host vswprintf.
 *   It has NO %n guard.  A %n specifier is passed straight through to the
 *   host formatter.  Wide %n is therefore exercised here ONLY with an owned,
 *   canary-guarded int* (never a bogus address).  Apple Clang also ignores
 *   format(wprintf,...) attributes, so there is no compile-time check either.
 *
 * The raw cplus_snprintf is never called with mismatched arguments here (that
 * would be undefined behaviour).  A separate /tmp compile-time probe checks
 * that Clang's format(printf,3,4) attribute diagnoses such a call.
 */
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>

#include "safe_format.h"
#include "safe_log.h"
#include "safe_runtime.h"

/* This corpus deliberately uses runtime (table-driven) format strings and a
 * deliberately argument-less "%s" probe; both are the subject under test. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wformat-security"
#endif

/* Local declaration: wide wrappers are intentionally kept out of safe_format.h. */
extern int cplus_swprintf(wchar_t *s, size_t n, const wchar_t *fmt, ...);

/* ---------------------------------------------------------------- helpers */

#define DEST_CAP   128u
#define LEAD_CAN   UINT32_C(0xA5C3E17B)
#define TRAIL_CAN  UINT32_C(0x5A3C1E7B)
#define FILL_BYTE  0xA5u
#define VIC_LEAD   UINT32_C(0x13572468)
#define VIC_TRAIL  UINT32_C(0x24681357)
#define VIC_VALUE  (-1234567)

typedef struct {
    uint32_t lead[2];
    char buf[DEST_CAP];
    uint32_t trail[2];
} guarded_dest_t;

typedef struct {
    uint32_t lead;
    int n;
    uint32_t trail;
} n_victim_t;

static int g_findings = 0;
static int g_cases = 0;

#define FINDING(...)                                                          \
    do {                                                                      \
        printf("FINDING: ");                                                  \
        printf(__VA_ARGS__);                                                  \
        fputc('\n', stdout);                                                  \
        ++g_findings;                                                         \
    } while (0)

static void init_dest(guarded_dest_t *g) {
    memset(g->buf, FILL_BYTE, sizeof g->buf);
    g->lead[0] = g->lead[1] = LEAD_CAN;
    g->trail[0] = g->trail[1] = TRAIL_CAN;
}

static void init_victim(n_victim_t *v) {
    v->lead = VIC_LEAD;
    v->n = VIC_VALUE;
    v->trail = VIC_TRAIL;
}

static int victim_ok(const n_victim_t *v) {
    return v->lead == VIC_LEAD && v->trail == VIC_TRAIL && v->n == VIC_VALUE;
}

/* No byte at or beyond dmax may be written.  Rejected calls must not write
 * anywhere at all.  Accepted calls must NUL-terminate at min(rc, dmax-1). */
static int dest_ok(const guarded_dest_t *g, size_t dmax, int rc, int rejected) {
    size_t i;
    if (g->lead[0] != LEAD_CAN || g->lead[1] != LEAD_CAN ||
        g->trail[0] != TRAIL_CAN || g->trail[1] != TRAIL_CAN) {
        return 0;
    }
    for (i = dmax; i < DEST_CAP; ++i) {
        if ((unsigned char)g->buf[i] != FILL_BYTE) return 0;
    }
    if (rejected) {
        for (i = 0; i < dmax; ++i) {
            if ((unsigned char)g->buf[i] != FILL_BYTE) return 0;
        }
    } else if (dmax > 0 && rc >= 0) {
        size_t z = (size_t)rc < dmax - 1u ? (size_t)rc : dmax - 1u;
        if (g->buf[z] != '\0') return 0;
    }
    return 1;
}

static int dest_fully_intact(const guarded_dest_t *g) {
    size_t i;
    if (g->lead[0] != LEAD_CAN || g->lead[1] != LEAD_CAN ||
        g->trail[0] != TRAIL_CAN || g->trail[1] != TRAIL_CAN) {
        return 0;
    }
    for (i = 0; i < DEST_CAP; ++i) {
        if ((unsigned char)g->buf[i] != FILL_BYTE) return 0;
    }
    return 1;
}

/* ---------------------------------------------------------- argument shapes */

enum shape {
    SH_NONE = 0,
    SH_INT,
    SH_INT2,
    SH_INT3,
    SH_LONG,
    SH_LLONG,
    SH_SIZE,
    SH_STR,
    SH_STR2,
    SH_STR3,
    SH_INT_STR,
    SH_STR_INT,
    SH_STR2_INT,
    SH_PTR,
    SH_PTR2,
    SH_PREC_STR,
    SH_PREC_STR_HUGE,
    SH_POSITIONAL,
    SH_N,
    SH_N2,
    SH_INT_N,
    SH_STR_N,
    SH_STR2_N,
    SH_PTR_N,
    SH_PREC_N,
    SH_COUNT
};

static const char kStr[] = "abcdefghijklmnopqrstuvwxyz0123456789";
static int g_obj = 7;

/* Every variadic call below has correct, format-matching argument types.
 * `extra` appends 0..2 harmless ints (printf ignores surplus arguments), which
 * makes arg_count vary deterministically across the corpus. */
#define CALLX(extra, fmt, dest, dmax, ...)                                    \
    ((extra) == 0 ? cplus_snprintf_s((dest), (dmax), (fmt), __VA_ARGS__)       \
     : (extra) == 1                                                            \
         ? cplus_snprintf_s((dest), (dmax), (fmt), __VA_ARGS__, 11)            \
         : cplus_snprintf_s((dest), (dmax), (fmt), __VA_ARGS__, 11, 22))

static int invoke(int shape, int extra, const char *fmt, char *dest,
                  size_t dmax, void *nt) {
    int *nptr = (int *)nt;
    switch (shape) {
    case SH_NONE:
        if (extra == 0) return cplus_snprintf_s(dest, dmax, fmt);
        if (extra == 1) return cplus_snprintf_s(dest, dmax, fmt, 11);
        return cplus_snprintf_s(dest, dmax, fmt, 11, 22);
    case SH_INT:          return CALLX(extra, fmt, dest, dmax, 42);
    case SH_INT2:         return CALLX(extra, fmt, dest, dmax, 42, -7);
    case SH_INT3:         return CALLX(extra, fmt, dest, dmax, 42, -7, 123456);
    case SH_LONG:         return CALLX(extra, fmt, dest, dmax, (long)-12345L);
    case SH_LLONG:        return CALLX(extra, fmt, dest, dmax,
                                       (long long)-9876543210LL);
    case SH_SIZE:         return CALLX(extra, fmt, dest, dmax, (size_t)12345u);
    case SH_STR:          return CALLX(extra, fmt, dest, dmax, kStr);
    case SH_STR2:         return CALLX(extra, fmt, dest, dmax, kStr, "xy");
    case SH_STR3:         return CALLX(extra, fmt, dest, dmax, kStr, "xy", "z");
    case SH_INT_STR:      return CALLX(extra, fmt, dest, dmax, 42, kStr);
    case SH_STR_INT:      return CALLX(extra, fmt, dest, dmax, kStr, 42);
    case SH_STR2_INT:     return CALLX(extra, fmt, dest, dmax, kStr, "xy", 42);
    case SH_PTR:          return CALLX(extra, fmt, dest, dmax, (void *)&g_obj);
    case SH_PTR2:         return CALLX(extra, fmt, dest, dmax,
                                       (void *)&g_obj, (void *)kStr);
    case SH_PREC_STR:     return CALLX(extra, fmt, dest, dmax, 5, kStr);
    case SH_PREC_STR_HUGE:return CALLX(extra, fmt, dest, dmax, INT_MAX, "short");
    case SH_POSITIONAL:   return CALLX(extra, fmt, dest, dmax, 42, kStr);
    case SH_N:            return CALLX(extra, fmt, dest, dmax, nptr);
    case SH_N2:           return CALLX(extra, fmt, dest, dmax, nptr, nptr);
    case SH_INT_N:        return CALLX(extra, fmt, dest, dmax, 42, nptr);
    case SH_STR_N:        return CALLX(extra, fmt, dest, dmax, kStr, nptr);
    case SH_STR2_N:       return CALLX(extra, fmt, dest, dmax, kStr, "xy", nptr);
    case SH_PTR_N:        return CALLX(extra, fmt, dest, dmax,
                                       (void *)&g_obj, nptr);
    case SH_PREC_N:       return CALLX(extra, fmt, dest, dmax, 5, kStr, nptr);
    default:              return -999;
    }
}

static int shape_argc(int shape) {
    switch (shape) {
    case SH_NONE:           return 0;
    case SH_INT:
    case SH_LONG:
    case SH_LLONG:
    case SH_SIZE:
    case SH_STR:
    case SH_PTR:
    case SH_PREC_STR:
    case SH_PREC_STR_HUGE:
    case SH_N:              return 1;
    case SH_INT2:
    case SH_INT_STR:
    case SH_STR_INT:
    case SH_STR2:
    case SH_PTR2:
    case SH_POSITIONAL:
    case SH_N2:
    case SH_INT_N:
    case SH_STR_N:
    case SH_PTR_N:
    case SH_PREC_N:         return 2;
    case SH_INT3:
    case SH_STR3:
    case SH_STR2_INT:
    case SH_STR2_N:         return 3;
    default:                return 0;
    }
}

/* ------------------------------------------------------------- format table */

typedef struct {
    const char *fmt;
    int shape;
    int rejects_n;   /* 1 when the format contains an active %n */
    int rare;        /* 1 for the ~0.16s width=999999999 case */
} fmt_entry_t;

static const fmt_entry_t kFormats[] = {
    /* plain literals, percent escapes, unicode / UTF-8, no varargs */
    { "",                       SH_NONE, 0, 0 },
    { "ok",                     SH_NONE, 0, 0 },
    { "A",                      SH_NONE, 0, 0 },
    { "%%",                     SH_NONE, 0, 0 },
    { "100%% done",             SH_NONE, 0, 0 },
    { "\u00e9",                 SH_NONE, 0, 0 },
    { "\U0001F600",             SH_NONE, 0, 0 },
    { "\xC3\xA9 caf\xC3\xA9",   SH_NONE, 0, 0 },
    { "line\nnext",             SH_NONE, 0, 0 },
    { "abc\tdef",               SH_NONE, 0, 0 },
    { "0123456789abcdef",       SH_NONE, 0, 0 },
    { "\u03b1\u03b2\u03b3",     SH_NONE, 0, 0 },
    /* integer conversions */
    { "%d",                     SH_INT,   0, 0 },
    { "%i",                     SH_INT,   0, 0 },
    { "%u",                     SH_INT,   0, 0 },
    { "%x",                     SH_INT,   0, 0 },
    { "%X",                     SH_INT,   0, 0 },
    { "%o",                     SH_INT,   0, 0 },
    { "%c",                     SH_INT,   0, 0 },
    { "%+d",                    SH_INT,   0, 0 },
    { "%08d",                   SH_INT,   0, 0 },
    { "%-12d",                  SH_INT,   0, 0 },
    { "%ld",                    SH_LONG,  0, 0 },
    { "%lld",                   SH_LLONG, 0, 0 },
    { "%zu",                    SH_SIZE,  0, 0 },
    { "%999999999d",            SH_INT,   0, 1 },
    /* strings, precision, positional */
    { "%s",                     SH_STR,   0, 0 },
    { "%.3s",                   SH_STR,   0, 0 },
    { "%10s",                   SH_STR,   0, 0 },
    { "%-20s",                  SH_STR,   0, 0 },
    { "%s:%s",                  SH_STR2,  0, 0 },
    { "%s%s%s",                 SH_STR3,  0, 0 },
    { "%.*s",                   SH_PREC_STR,       0, 0 },
    { "%.*s",                   SH_PREC_STR_HUGE,  0, 0 },
    { "%2$s",                   SH_POSITIONAL, 0, 0 },
    { "%2$s %1$d",              SH_POSITIONAL, 0, 0 },
    { "%1$d-%2$s",              SH_POSITIONAL, 0, 0 },
    /* pointers and mixed conversions */
    { "%p",                     SH_PTR,      0, 0 },
    { "%p %p",                  SH_PTR2,     0, 0 },
    { "%d-%s",                  SH_INT_STR,  0, 0 },
    { "%s-%d",                  SH_STR_INT,  0, 0 },
    { "%d/%d/%d",               SH_INT3,     0, 0 },
    { "%s/%s/%d",               SH_STR2_INT, 0, 0 },
    /* %n family: must be REJECTED before any vararg is consumed */
    { "%n",                     SH_N,        1, 0 },
    { "%hhn",                   SH_N,        1, 0 },
    { "%hn",                    SH_N,        1, 0 },
    { "%ln",                    SH_N,        1, 0 },
    { "%lln",                   SH_N,        1, 0 },
    { "%zn",                    SH_N,        1, 0 },
    { "%jn",                    SH_N,        1, 0 },
    { "x%n",                    SH_N,        1, 0 },
    { "%%%n",                   SH_N,        1, 0 },
    { "%2$n",                   SH_N2,       1, 0 },
    { "%n%n",                   SH_N2,       1, 0 },
    { "%d%n",                   SH_INT_N,    1, 0 },
    { "%1$d%n",                 SH_INT_N,    1, 0 },
    { "%s%n",                   SH_STR_N,    1, 0 },
    { "%s%s%n",                 SH_STR2_N,   1, 0 },
    { "%p%n",                   SH_PTR_N,    1, 0 },
    { "%.*s%n",                 SH_PREC_N,   1, 0 },
    { "%999999999d%n",          SH_INT_N,    1, 0 },
    { "prefix-%hhn-suffix",     SH_N,        1, 0 }
};

static const size_t kCaps[] = {
    0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u, 15u, 16u, 17u, 31u, 32u, 63u, 64u, 127u
};

typedef struct {
    const char *fmt;
    size_t dmax;
    int argc;
} tuple_t;

#define MAX_TUPLES 2048
static tuple_t g_tuples[MAX_TUPLES];
static int g_ntuples = 0;

static void record_tuple(const char *fmt, size_t dmax, int argc) {
    if (g_ntuples < MAX_TUPLES) {
        g_tuples[g_ntuples].fmt = fmt;
        g_tuples[g_ntuples].dmax = dmax;
        g_tuples[g_ntuples].argc = argc;
        ++g_ntuples;
    }
}

static void verify_tuples_unique(void) {
    int i, j;
    for (i = 0; i < g_ntuples; ++i) {
        for (j = i + 1; j < g_ntuples; ++j) {
            if (g_tuples[i].dmax == g_tuples[j].dmax &&
                g_tuples[i].argc == g_tuples[j].argc &&
                strcmp(g_tuples[i].fmt, g_tuples[j].fmt) == 0) {
                FINDING("duplicate tuple (fmt=%s dmax=%lu argc=%d)",
                        g_tuples[i].fmt, (unsigned long)g_tuples[i].dmax,
                        g_tuples[i].argc);
                continue;
            }
        }
    }
}

static void run_narrow_corpus(void) {
    size_t fi, ci;
    for (fi = 0; fi < sizeof kFormats / sizeof kFormats[0]; ++fi) {
        const fmt_entry_t *e = &kFormats[fi];
        for (ci = 0; ci < sizeof kCaps / sizeof kCaps[0]; ++ci) {
            size_t dmax = kCaps[ci];
            int extra = (int)((fi + ci) % 3u);
            int argc = shape_argc(e->shape) + extra;
            guarded_dest_t g;
            n_victim_t v;
            int rc;
            if (e->rare && ci >= 2u) continue;   /* keep runtime well under 10s */
            init_dest(&g);
            init_victim(&v);
            errno = 0;
            rc = invoke(e->shape, extra, e->fmt, g.buf, dmax, &v.n);
            ++g_cases;
            record_tuple(e->fmt, dmax, argc);

            if (e->rejects_n) {
                if (rc != -1 || errno != EINVAL) {
                    FINDING("%%n not rejected: fmt=[%s] dmax=%lu argc=%d "
                            "rc=%d errno=%d (expected -1/EINVAL)",
                            e->fmt, (unsigned long)dmax, argc, rc, errno);
                }
                if (!victim_ok(&v)) {
                    FINDING("%%n wrote through bogus target: fmt=[%s] dmax=%lu "
                            "rc=%d", e->fmt, (unsigned long)dmax, rc);
                }
                if (!dest_fully_intact(&g)) {
                    FINDING("rejected format touched destination: fmt=[%s] "
                            "dmax=%lu rc=%d", e->fmt, (unsigned long)dmax, rc);
                }
            } else {
                if (rc < 0) {
                    FINDING("accepted format returned error: fmt=[%s] "
                            "dmax=%lu argc=%d rc=%d errno=%d",
                            e->fmt, (unsigned long)dmax, argc, rc, errno);
                }
                if (!victim_ok(&v)) {
                    FINDING("non-%%n format modified sentinel target: fmt=[%s] "
                            "dmax=%lu", e->fmt, (unsigned long)dmax);
                }
                if (!dest_ok(&g, dmax, rc, 0)) {
                    FINDING("destination canary/dmax violation: fmt=[%s] "
                            "dmax=%lu argc=%d rc=%d",
                            e->fmt, (unsigned long)dmax, argc, rc);
                }
            }
        }
    }
}

/* ------------------------------------- embedded-NUL formats (bounded APIs) */

static void run_embedded_nul_corpus(void) {
    /* Format buffers that contain an embedded NUL.  The checked API is a
     * length-bounded C-string API: everything after the first NUL is invisible
     * to both the %n scanner and vsnprintf, so a hidden "%n" is inert. */
    static const unsigned char f1[] = { '%', 'd', 0, '%', 'n', 0 };
    static const unsigned char f2[] = { 'q', 'k', '!', 0, '%', 'n', 0 };
    static const unsigned char f3[] = { '%', 'n', 0, 'x', 'y', 0 };
    static const unsigned char f4[] = { '%', 's', 0, '%', 'n', 0 };
    static const unsigned char f5[] = { '%', '.', '*', 's', 0, '%', 'n', 0 };
    struct { const unsigned char *buf; const char *label;
             int shape; int reject; } cases[] = {
        { f1, "NUL0:%d",   SH_INT,       0 },
        { f2, "NUL1:lit",  SH_NONE,      0 },
        { f3, "NUL2:%n",   SH_N,         1 },
        { f4, "NUL3:%s",   SH_STR,       0 },
        { f5, "NUL4:%.*s", SH_PREC_STR,  0 }
    };
    size_t i, ci;
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        for (ci = 0; ci < sizeof kCaps / sizeof kCaps[0]; ++ci) {
            size_t dmax = kCaps[ci];
            guarded_dest_t g;
            n_victim_t v;
            int rc;
            init_dest(&g);
            init_victim(&v);
            errno = 0;
            rc = invoke(cases[i].shape, (int)(i % 3u),
                        (const char *)cases[i].buf, g.buf, dmax, &v.n);
            ++g_cases;
            record_tuple(cases[i].label, dmax,
                         shape_argc(cases[i].shape) + (int)(i % 3u));
            if (cases[i].reject) {
                if (rc != -1 || errno != EINVAL || !dest_fully_intact(&g) ||
                    !victim_ok(&v)) {
                    FINDING("embedded-NUL reject case %lu failed: rc=%d errno=%d",
                            (unsigned long)i, rc, errno);
                }
            } else if (rc < 0 || !dest_ok(&g, dmax, rc, 0) || !victim_ok(&v)) {
                FINDING("embedded-NUL case %lu failed: dmax=%lu rc=%d",
                        (unsigned long)i, (unsigned long)dmax, rc);
            }
        }
    }
}

/* --------------------------------------------- forked UB / wild-pointer probes */

#if defined(__APPLE__) || defined(__unix__)
static void wait_probe(const char *label, pid_t pid) {
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) {
        FINDING("probe %s: waitpid failed", label);
        return;
    }
    if (WIFSIGNALED(st)) {
        FINDING("probe %s crashed (signal %d)", label, WTERMSIG(st));
    } else if (WIFEXITED(st) && WEXITSTATUS(st) != 0) {
        FINDING("probe %s corrupted canary or returned undefined value", label);
    }
    ++g_cases;
}

/* Wild pointer as the %n target: a real write-through would SIGSEGV in the
 * child.  The checked API is expected to reject before touching the vararg. */
static void probe_wild_n(const char *fmt) {
    pid_t pid;
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        guarded_dest_t g;
        int rc;
        init_dest(&g);
        cplus_runtime_set_violation_trap(0);
        errno = 0;
        rc = cplus_snprintf_s(g.buf, 32u, fmt, (int *)(uintptr_t)1);
        _exit(rc == -1 && errno == EINVAL && dest_fully_intact(&g) ? 0 : 1);
    }
    if (pid < 0) {
        FINDING("probe_wild_n(%s): fork failed", fmt);
    } else {
        wait_probe(fmt, pid);
    }
}

/* Bare "%s" with no argument is UB if it reaches vsnprintf.  It is contained
 * in a child; a crash or canary corruption is reported as a FINDING. */
static void probe_missing_arg(const char *fmt) {
    pid_t pid;
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        guarded_dest_t g;
        int rc;
        init_dest(&g);
        cplus_runtime_set_violation_trap(0);
        errno = 0;
        rc = cplus_snprintf_s(g.buf, 32u, fmt);
        _exit(dest_ok(&g, 32u, rc, 0) ? 0 : 1);
    }
    if (pid < 0) {
        FINDING("probe_missing_arg(%s): fork failed", fmt);
    } else {
        wait_probe(fmt, pid);
    }
}
#endif

static void run_fork_probes(void) {
#if defined(__APPLE__) || defined(__unix__)
    static const char *const wild[] = { "%n", "%hhn", "%lln", "x%n", "%%%n" };
    static const char *const missing[] = { "%s", "%d", "%p", "%n", "%.*s" };
    size_t i;
    for (i = 0; i < sizeof wild / sizeof wild[0]; ++i) probe_wild_n(wild[i]);
    /* A deliberately argument-less "%s" makes UBSan itself report a runtime
     * error in the forked child, which is a harness artifact rather than a
     * library finding. Those probes are exercised in the plain build only. */
#if defined(__has_feature)
#if __has_feature(undefined_behavior_sanitizer)
    printf("note: argument-less format probes skipped under UBSan\n");
#else
    for (i = 0; i < sizeof missing / sizeof missing[0]; ++i) {
        probe_missing_arg(missing[i]);
    }
#endif
#else
    for (i = 0; i < sizeof missing / sizeof missing[0]; ++i) {
        probe_missing_arg(missing[i]);
    }
#endif
#endif
}

/* ----------------------------------------------------------- wide formats */

#define WCAP 32u
typedef struct {
    uint32_t lead;
    wchar_t buf[WCAP];
    uint32_t trail;
} wguarded_t;

static void init_wdest(wguarded_t *w) {
    size_t i;
    for (i = 0; i < WCAP; ++i) w->buf[i] = (wchar_t)0x4E2Du;
    w->lead = LEAD_CAN;
    w->trail = TRAIL_CAN;
}

static int wdest_ok(const wguarded_t *w, size_t n) {
    size_t i;
    if (w->lead != LEAD_CAN || w->trail != TRAIL_CAN) return 0;
    for (i = n; i < WCAP; ++i) {
        if (w->buf[i] != (wchar_t)0x4E2Du) return 0;
    }
    return 1;
}

static void run_wide_corpus(void) {
    struct wcase { const wchar_t *fmt; int shape; int has_n; };
    static const struct wcase wcases[] = {
        { L"",              SH_NONE, 0 },
        { L"ok",            SH_NONE, 0 },
        { L"%%",            SH_NONE, 0 },
        { L"unicode:\u00e9", SH_NONE, 0 },
        { L"%d",            SH_INT,  0 },
        { L"%s",            SH_STR,  0 },
        { L"%p",            SH_PTR,  0 },
        { L"%d-%s",         SH_INT_STR, 0 },
        { L"%n",            SH_N,    1 },
        { L"abc%n",         SH_N,    1 },
        { L"x%n",           SH_N,    1 },
        { L"%d%n",          SH_INT_N, 1 }
    };
    static const size_t wcaps[] = { 0u, 1u, 2u, 4u, 8u, 16u, 31u };
    size_t i, ci;
    for (i = 0; i < sizeof wcases / sizeof wcases[0]; ++i) {
        for (ci = 0; ci < sizeof wcaps / sizeof wcaps[0]; ++ci) {
            wguarded_t w;
            n_victim_t v;
            size_t n = wcaps[ci];
            int rc;
            init_wdest(&w);
            init_victim(&v);
            errno = 0;
            /* Wide dispatch: only owned, valid pointers are ever passed. */
            switch (wcases[i].shape) {
            case SH_NONE:     rc = cplus_swprintf(w.buf, n, wcases[i].fmt); break;
            case SH_INT:      rc = cplus_swprintf(w.buf, n, wcases[i].fmt, 42); break;
            case SH_STR:      rc = cplus_swprintf(w.buf, n, wcases[i].fmt, "narrow"); break;
            case SH_PTR:      rc = cplus_swprintf(w.buf, n, wcases[i].fmt,
                                                  (void *)&g_obj); break;
            case SH_INT_STR:  rc = cplus_swprintf(w.buf, n, wcases[i].fmt, 42,
                                                  "narrow"); break;
            case SH_N:        rc = cplus_swprintf(w.buf, n, wcases[i].fmt,
                                                  (int *)&v.n); break;
            case SH_INT_N:    rc = cplus_swprintf(w.buf, n, wcases[i].fmt, 42,
                                                  (int *)&v.n); break;
            default:          rc = -999; break;
            }
            ++g_cases;
            if (!wdest_ok(&w, n)) {
                FINDING("wide destination canary violation: fmt=%p n=%lu rc=%d",
                        (const void *)wcases[i].fmt, (unsigned long)n, rc);
            }
            if (w.lead != LEAD_CAN || w.trail != TRAIL_CAN) {
                FINDING("wide guard corrupted: fmt=%p n=%lu",
                        (const void *)wcases[i].fmt, (unsigned long)n);
            }
            /* No guard exists in cplus_swprintf: %n is passed to the host.
             * We only verify that our OWN target was used, never a wild one. */
            if (wcases[i].has_n && n > 0u) {
                if (v.lead != VIC_LEAD || v.trail != VIC_TRAIL) {
                    FINDING("wide %%n wrote outside owned target: fmt=%p n=%lu",
                            (const void *)wcases[i].fmt, (unsigned long)n);
                }
            } else if (!wcases[i].has_n && !victim_ok(&v)) {
                FINDING("wide non-%%n format modified sentinel: fmt=%p n=%lu",
                        (const void *)wcases[i].fmt, (unsigned long)n);
            }
            (void)rc; /* host may legitimately return -1 on truncation */
        }
    }
}

/* ------------------------------------------------------------------- main */

int main(void) {
    /* The SafeCRT violation trap is ON by default (_exit(70)).  Disable it so
     * the documented -1/EINVAL rejection contract is observable in-process. */
    cplus_runtime_set_violation_trap(0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    /* Do NOT redirect stderr: cplus_runtime_violation() writes its own
     * diagnostic and (on the first redirected write) clobbers errno, which
     * would hide the real -1/EINVAL contract under test.  Expected
     * [CPLUS-SAFE] diagnostics therefore appear on stderr. */

    run_narrow_corpus();
    run_embedded_nul_corpus();
    run_fork_probes();
    run_wide_corpus();
    verify_tuples_unique();

    printf("note: checked cplus_snprintf_s/cplus_sprintf_s REJECT %%n "
           "(return -1, errno=EINVAL, no write, no deref)\n");
    printf("note: cplus_swprintf has NO %%n guard (host pass-through); "
           "wide %%n used only an owned int*\n");
    printf("corpus_format: %d cases ok\n", g_cases);
    return g_findings ? 1 : 0;
}
