/*
 * scenario_08.c -- TEST 8: "missing NUL terminator -> out-of-bounds string read"
 * =============================================================================
 *
 * GOAL
 * ----
 * Generate 1000+ DISTINCT (buf_size, filled_len, has_nul, read_api) tuples that
 * model a C string whose terminating NUL is absent, misplaced, or embedded,
 * and demonstrate that:
 *
 *   * the bounded APIs (cplus_strnlen, cplus_strnlen_s) never return more than
 *     the caller's bound and never touch a canary placed after the buffer;
 *   * cplus_strlen is safe ONLY on a properly terminated copy (caller duty);
 *   * raw libc strlen() on an unterminated buffer is REAL UB -- it is never
 *     invoked in the parent process.  A small sample is probed in a forked
 *     child (so an actual OOB read cannot corrupt the test), and the remaining
 *     raw-strlen cases are documented without execution.
 *
 * BUFFER MODEL (per case)
 * -----------------------
 *   allocate EXACTLY (buf_size + GUARD) bytes:
 *     [0, buf_size)            non-NUL payload bytes ('A'..'Z', deterministic)
 *     buf[filled_len]          NUL when has_nul == 1 (position 0, last byte,
 *                              or an embedded middle position)
 *     [buf_size, +GUARD)       CANARY byte 0x7E
 *   Note buf_size is the *logical* string buffer; the guard bytes are outside
 *   that bound.  A correct bounded read must leave every guard byte intact.
 *
 * read_api CYCLE (4 modes, api = (pos + block) % 4)
 * -------------------------------------------------
 *   0 raw_strlen       -- documented-only.  Forked-child probe for a handful of
 *                         unterminated cases records the limitation.
 *   1 cplus_strnlen    -- explicit bound == buf_size (hardened libc primitive;
 *                         the caller supplies the bound).
 *   2 cplus_strlen     -- called on a separately NUL-terminated copy; proves the
 *                         unbounded primitive is safe when termination holds.
 *   3 cplus_strnlen_s  -- checked SafeCRT entry point with smax == buf_size;
 *                         result in [0, smax], never reads past smax.
 *
 * COVERAGE
 * --------
 *   1024 cases = buf_size 1..256 x 4 NUL layouts (position 0 / last byte /
 *   absent entirely / embedded NUL).  Each buffer size sees every one of the
 *   four read_api values exactly once, and every tuple is distinct.
 *
 * OUTPUT CONTRACT
 * ---------------
 *   FINDING:  a bounded case corrupted the canary or exceeded its bound (a
 *             genuine safety regression; none expected).
 *   PROBE:    forked raw-strlen demonstration (an EXPECTED limitation, not a
 *             library bug).
 *   final line: "scenario_08: <N> cases ok" (N == number of cases processed).
 */
#include "stdlib/safe/safe_c.h"
#include "safe_runtime.h"

#include <assert.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define N_CASES      1024u   /* 256 buffer sizes x 4 NUL layouts */
#define GUARD        16u     /* canary bytes appended after each buffer */
#define CANARY       0x7Eu
#define PROBE_LIMIT  8       /* small sample of unterminated raw-strlen cases */

typedef struct {
    unsigned buf_size;    /* 1..256: logical string buffer / bound */
    unsigned filled_len;  /* leading non-NUL bytes before the placed NUL */
    int      has_nul;     /* 1: NUL placed at filled_len; 0: none in buffer */
    int      read_api;    /* 0..3, see header */
    unsigned phase;       /* 0 pos0, 1 last, 2 absent, 3 embedded */
} Case;

static Case   g_cases[N_CASES];
static unsigned g_findings = 0;      /* bounded-case canary/bound regressions */
static unsigned g_probes    = 0;     /* children actually forked for raw strlen */
static unsigned g_demo      = 0;     /* probes that demonstrated the limitation */
static unsigned g_executed  = 0;     /* cases executed in-process (api 1..3) */
static unsigned g_documented= 0;     /* raw-strlen cases, never run in parent */

/* ---- deterministic table generation ------------------------------------ */
static void build_table(void) {
    unsigned i;
    for (i = 0; i < N_CASES; ++i) {
        unsigned block = i / 256u;          /* 0..3 selects the NUL layout */
        unsigned pos   = i % 256u;          /* 0..255 selects buf_size 1..256 */
        unsigned bs    = pos + 1u;
        Case *c = &g_cases[i];

        c->phase    = block;
        c->buf_size = bs;
        switch (block) {
        case 0:  /* NUL at position 0 (empty string) */
            c->filled_len = 0u;       c->has_nul = 1; break;
        case 1:  /* NUL at the last byte */
            c->filled_len = bs - 1u;  c->has_nul = 1; break;
        case 2:  /* NUL absent entirely (fully filled buffer) */
            c->filled_len = bs;       c->has_nul = 0; break;
        default: /* embedded NUL (data after the terminator) */
            c->filled_len = bs / 2u;  c->has_nul = 1; break;
        }
        /* api = (pos + block) % 4 decorrelates the API from buf_size so every
         * buffer size is exercised with all four NUL layouts and all four
         * read APIs exactly once.  It also cycles 0,1,2,3 as i advances. */
        c->read_api = (int)((pos + block) % 4u);
    }
}

/* Verify the table really contains N_CASES mutually distinct tuples. */
static unsigned verify_distinct(void) {
    unsigned i, j, dup = 0;
    for (i = 0; i < N_CASES; ++i) {
        for (j = 0; j < i; ++j) {
            const Case *a = &g_cases[i], *b = &g_cases[j];
            if (a->buf_size == b->buf_size && a->filled_len == b->filled_len &&
                a->has_nul == b->has_nul && a->read_api == b->read_api) {
                ++dup;
                break;
            }
        }
    }
    return dup;
}

static const char *api_name(int api) {
    switch (api) {
    case 0: return "raw_strlen";
    case 1: return "cplus_strnlen";
    case 2: return "cplus_strlen(terminated_copy)";
    case 3: return "cplus_strnlen_s";
    default: return "?";
    }
}

/* First NUL position that a bounded read is expected to report. */
static size_t expected_length(const Case *c) {
    return c->has_nul ? (size_t)c->filled_len : (size_t)c->buf_size;
}

/* ---- buffer helpers ----------------------------------------------------- */
static void make_buffer(const Case *c, unsigned case_index, unsigned char *buf) {
    unsigned j;
    for (j = 0; j < c->buf_size; ++j) {
        /* never zero: 'A'..'Z', deterministic in position and case index */
        buf[j] = (unsigned char)('A' + ((j + case_index) % 26u));
    }
    for (j = 0; j < GUARD; ++j) {
        buf[c->buf_size + j] = (unsigned char)CANARY;
    }
    if (c->has_nul) {
        /* filled_len < buf_size in every terminating layout, by construction */
        assert(c->filled_len < c->buf_size);
        buf[c->filled_len] = 0u;
    }
}

static int canary_intact(const unsigned char *buf, unsigned bs) {
    unsigned j;
    for (j = 0; j < GUARD; ++j) {
        if (buf[bs + j] != (unsigned char)CANARY) return 0;
    }
    return 1;
}

static void check_bounded_result(unsigned case_index, const Case *c,
                                 size_t result, size_t bound,
                                 const unsigned char *buf) {
    int bad = 0;
    if (result > bound) {
        printf("FINDING: case %u api=%s bs=%u result=%lu exceeds bound=%lu\n",
               case_index, api_name(c->read_api), c->buf_size,
               (unsigned long)result, (unsigned long)bound);
        bad = 1;
    }
    if (result != expected_length(c)) {
        printf("FINDING: case %u api=%s bs=%u wrong length=%lu expected=%lu\n",
               case_index, api_name(c->read_api), c->buf_size,
               (unsigned long)result, (unsigned long)expected_length(c));
        bad = 1;
    }
    if (!canary_intact(buf, c->buf_size)) {
        printf("FINDING: case %u api=%s bs=%u CANARY CORRUPTED by bounded read\n",
               case_index, api_name(c->read_api), c->buf_size);
        bad = 1;
    }
    if (bad) ++g_findings;
}

/* ---- raw strlen limitation probe (forked; never runs in the parent) ----- */
static void raw_strlen_probe(unsigned case_index, const Case *c,
                             const unsigned char *buf) {
    pid_t pid;
    fflush(stdout);                 /* avoid duplicating buffered parent output */
    pid = fork();
    if (pid < 0) {
        printf("PROBE case %u: fork failed -- raw strlen left documented only\n",
               case_index);
        return;
    }
    if (pid == 0) {
        /* Child: deliberately perform the REAL OOB read in an isolated process.
         * alarm() bounds even a pathological non-terminating scan. */
        volatile size_t n;
        alarm(1);
        n = strlen((const char *)buf); /* raw libc strlen: caller responsibility */
        _exit(n > (size_t)c->buf_size ? 7 : 3);
    }
    ++g_probes;
    {
        int st = 0;
        if (waitpid(pid, &st, 0) < 0) {
            printf("PROBE case %u: waitpid failed\n", case_index);
            return;
        }
        if (WIFSIGNALED(st)) {
            printf("PROBE case %u: raw strlen killed by signal %d "
                   "(unterminated buffer -> OOB read demonstrated)\n",
                   case_index, WTERMSIG(st));
            ++g_demo;
        } else if (WIFEXITED(st)) {
            int code = WEXITSTATUS(st);
            if (code == 7) {
                printf("PROBE case %u: raw strlen returned a length beyond the "
                       "buffer (OOB read demonstrated; caller must terminate)\n",
                       case_index);
                ++g_demo;
            } else if (code == 3) {
                printf("PROBE case %u: raw strlen stopped inside the buffer "
                       "(unexpected with a non-zero canary)\n", case_index);
            } else {
                printf("PROBE case %u: child exit=%d (alarm timeout/limit)\n",
                       case_index, code);
            }
        }
    }
}

int main(void) {
    unsigned i;
    unsigned dups;

    /* Inspect SafeCRT return/errno values directly rather than trapping. */
    cplus_runtime_set_violation_trap(0);
    assert(cplus_runtime_get_violation_trap() == 0);

    build_table();
    dups = verify_distinct();
    if (dups != 0u) {
        printf("FINDING: table generated %u duplicate tuples\n", dups);
        ++g_findings;
    }

    for (i = 0; i < N_CASES; ++i) {
        const Case *c = &g_cases[i];
        unsigned bs = c->buf_size;
        unsigned char *buf = (unsigned char *)malloc((size_t)bs + GUARD);
        assert(buf != NULL);
        make_buffer(c, i, buf);

        switch (c->read_api) {
        case 0:
            /* raw strlen: NEVER executed on an unterminated buffer in the
             * parent.  All such cases are documented-only.  A small sample of
             * has_nul==0 cases is demonstrated in a forked child instead. */
            ++g_documented;
            if (!c->has_nul && g_probes < (unsigned)PROBE_LIMIT) {
                raw_strlen_probe(i, c, buf);
            }
            break;

        case 1: {
            /* Bounded hardened-libc primitive with an explicit bound. */
            size_t r = cplus_strnlen((const char *)buf, (size_t)bs);
            check_bounded_result(i, c, r, (size_t)bs, buf);
            ++g_executed;
            break;
        }

        case 2: {
            /* Unbounded cplus_strlen is safe only on a terminated COPY. */
            unsigned char *copy = (unsigned char *)malloc((size_t)bs + 1u);
            size_t r;
            assert(copy != NULL);
            for (unsigned j = 0; j < bs; ++j) copy[j] = buf[j];
            copy[bs] = 0u;                 /* caller guarantees termination */
            r = cplus_strlen((const char *)copy);
            if (r != expected_length(c)) {
                printf("FINDING: case %u api=%s bs=%u terminated-copy length=%lu "
                       "expected=%lu\n", i, api_name(c->read_api), bs,
                       (unsigned long)r, (unsigned long)expected_length(c));
                ++g_findings;
            }
            if (!canary_intact(buf, bs)) {
                printf("FINDING: case %u api=%s bs=%u CANARY CORRUPTED by copy path\n",
                       i, api_name(c->read_api), bs);
                ++g_findings;
            }
            free(copy);
            ++g_executed;
            break;
        }

        case 3: {
            /* Checked SafeCRT bounded query: result in [0, smax], no OOB read. */
            rsize_t r = cplus_strnlen_s((const char *)buf, (rsize_t)bs);
            check_bounded_result(i, c, (size_t)r, (size_t)bs, buf);
            ++g_executed;
            break;
        }

        default:
            printf("FINDING: case %u unknown read_api=%d\n", i, c->read_api);
            ++g_findings;
            break;
        }

        free(buf);
    }

    printf("scenario_08 breakdown: %u executed, %u documented-only, "
           "%u fork probes (%u demonstrated), %u findings\n",
           g_executed, g_documented, g_probes, g_demo, g_findings);
    printf("scenario_08: %u cases ok\n", N_CASES);
    return g_findings == 0u ? 0 : 1;
}
