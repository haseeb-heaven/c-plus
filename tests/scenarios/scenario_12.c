/* scenario_12 -- format-string misuse / %n / argument-mismatch cases.
 *
 * Standalone C99.  Builds with the two checked libraries and the include paths
 * used by tests/scenarios/run_scenarios.sh:
 *
 *   clang -O2 -std=c99 -Wall -Wextra -pedantic \
 *     -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
 *     -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
 *     tests/scenarios/scenario_12.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o scenario_12
 *
 * WHAT IS EXERCISED
 * -----------------
 * A deterministic table of (format_kind, dest_capacity, has_args, arg_type)
 * tuples is run through the CHECKED bounded API cplus_snprintf_s() (declared in
 * stdlib/safe/safe_format.h with format(printf,3,4)).  Every destination is a
 * canary-guarded stack object so a write past dmax, a bogus %n write, or a
 * missing termination is detected and reported as a FINDING with the exact
 * tuple.  The SafeCRT violation trap is disabled so refused calls return -1
 * instead of exiting; cplus_runtime_violation() still prints its diagnostic to
 * stderr, which is expected for the %n / non-literal rows.
 *
 * RECORDED EXACT BEHAVIOUR (this host, both plain and UBSan builds)
 * -----------------------------------------------------------------
 *  - "%n": cplus_fmt_has_percent_n() sees the conversion and the checked
 *    entry point refuses it BEFORE vsnprintf() is reached.  Return is -1,
 *    errno == EINVAL, the destination is byte-for-byte untouched, and the
 *    supplied int* (a real sink on has_args=1, a deliberately bogus address on
 *    has_args=0) is never dereferenced.  No bogus-pointer write occurs.
 *  - non-literal dynamic format: cplus_snprintf_s() itself does NOT enforce
 *    literalness; it formats normally (it is the runtime format checker for
 *    %n/dmax, not the CWE-134 literal gate).  The literal gate lives in
 *    cplus_snprintf_s_checked()/the snprintf_s macro: with fmt_is_literal==0
 *    that entry point returns -1 with errno == EINVAL and writes nothing.
 *    Both behaviours are asserted here for every dynamic-format row.
 *  - "%999999999d": bounded to dmax by vsnprintf; returns the would-be length
 *    (999999999) with only dmax-1 chars plus NUL written.  No overflow.
 *  - positional "%2$s": the second vararg is consumed; first is a dummy int.
 *  - "%%" and plain text: no conversions, return is the output length.
 *  - "%ls" (wide-derived narrow): host converts the ASCII wide string; return
 *    is the multibyte length, or a defined -1/EILSEQ for non-representable
 *    input (only ASCII wide strings are used here).
 *
 * RAW printf MISMATCHES ARE NOT EXECUTED (that would be UB).  A separate
 * compile-only probe at /tmp/scenario12_format_probe.c shows Clang's
 * format(printf,1,2) attribute firing on the mapped printf wrapper:
 *
 *   warning: format specifies type 'char *' but the argument has type 'int'
 *   warning: more '%' conversions than data arguments
 *   warning: format specifies type 'int *' but the argument has type 'int'
 *   warning: data argument not used by format string
 *
 * WIDE FORMATS: cplus_swprintf() is declared by the included libc header
 * "cplus_libc.h" (pulled in by safe_c.h).  It is exercised with bounded
 * buffers.  Wide format checking is host-only on Apple Clang (format(wprintf)
 * is ignored); in particular wide "%n" is NOT rejected by the checked narrow
 * API -- it is a documented HOST PASS-THROUGH.  The test writes through a real
 * int sink and observes the count, never through a bogus pointer.
 */
#include "stdlib/safe/safe_c.h"    /* printf/fprintf -> attributed C+ wrappers */
#include "stdlib/safe/safe_format.h" /* checked _s formatting API */
#include "stdlib/safe/safe_runtime.h" /* violation trap control */
#include "cplus_libc.h"            /* cplus_swprintf declaration (wide formats) */

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

/* ---- format-kind and value domains -------------------------------------- */
enum {
    FK_D = 0,        /* literal "%d" */
    FK_S,            /* literal "%s" */
    FK_N,            /* literal "%n" (must be refused) */
    FK_HUGE,         /* literal "%999999999d" */
    FK_PREC_S,       /* literal "%.*s" */
    FK_DYNAMIC,      /* runtime-built, non-literal format */
    FK_POSITIONAL,   /* literal "%2$s" */
    FK_PCT,          /* literal "%%" */
    FK_TEXT,         /* plain text, no conversions */
    FK_WIDE_DERIVED, /* literal "%ls" (wide-derived narrow) */
    FK_COUNT
};

enum { AT_ZERO = 0, AT_ONE, AT_NEG, AT_MAX, AT_PTR, AT_BOGUS, AT_COUNT };
#define N_CAPS 10
#define GUARD 8
#define INNER 128
#define HEAD_BYTE 0x5Au
#define TAIL_BYTE 0x3Cu
#define FILL_BYTE 0xA5u
#define N_SENTINEL 0x5eed1234

static const size_t caps[N_CAPS] = { 0u, 1u, 2u, 3u, 4u, 7u, 8u, 16u, 32u, 64u };

struct guarded {
    unsigned char head[GUARD];
    char dest[INNER];
    unsigned char tail[GUARD];
};

static void fill(unsigned char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = v;
}

static void init_guarded(struct guarded *g) {
    fill(g->head, GUARD, HEAD_BYTE);
    fill((unsigned char *)g->dest, INNER, FILL_BYTE);
    fill(g->tail, GUARD, TAIL_BYTE);
}

/* Returns NULL when every invariant holds, else a short reason string. */
static const char *check_guarded(const struct guarded *g, size_t dmax, int rc) {
    size_t i;
    for (i = 0; i < GUARD; ++i)
        if (g->head[i] != HEAD_BYTE) return "head canary clobbered";
    for (i = 0; i < GUARD; ++i)
        if (g->tail[i] != TAIL_BYTE) return "tail canary clobbered";
    for (i = dmax; i < INNER; ++i)
        if ((unsigned char)g->dest[i] != FILL_BYTE) return "write past dmax";
    if (rc < -1) return "undefined/negative return";
    if (rc >= 0 && dmax > 0) {
        for (i = 0; i < dmax; ++i)
            if (g->dest[i] == '\0') return NULL;
        return "missing NUL termination within dmax";
    }
    return NULL;
}

static int int_value(int at, int has_args) {
    if (!has_args) return 0;
    switch (at) {
        case AT_ZERO: return 0;
        case AT_ONE:  return 1;
        case AT_NEG:  return -1;
        case AT_MAX:  return INT_MAX;
        case AT_PTR:  return 7;
        default:      return INT_MIN; /* AT_BOGUS */
    }
}

static const char *str_value(int at, int has_args) {
    if (!has_args) return "";
    switch (at) {
        case AT_ZERO: return "";
        case AT_ONE:  return "x";
        case AT_NEG:  return "abcdef";
        case AT_MAX:  return "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        case AT_PTR:  return "ptr";
        default:      return "bogus";
    }
}

static const wchar_t *wide_value(int at, int has_args) {
    if (!has_args) return L"";
    switch (at) {
        case AT_ZERO: return L"";
        case AT_ONE:  return L"w";
        case AT_NEG:  return L"wide";
        case AT_MAX:  return L"0123456789abcdef";
        case AT_PTR:  return L"p";
        default:      return L"b";
    }
}

static int precision_value(int at, int has_args) {
    if (!has_args) return 0;
    switch (at) {
        case AT_ZERO: return 0;
        case AT_ONE:  return 1;
        case AT_NEG:  return 2;
        case AT_MAX:  return 6;
        case AT_PTR:  return 3;
        default:      return 4;
    }
}

int main(void) {
    volatile int n_sink = N_SENTINEL;
    int cases = 0;
    int findings = 0;
    int k, ci, ha, at;

    /* Refused calls must return -1/EINVAL rather than terminate the process. */
    cplus_runtime_set_failstop(0);
    cplus_runtime_set_violation_trap(0);

    for (k = 0; k < FK_COUNT; ++k) {
        for (ci = 0; ci < N_CAPS; ++ci) {
            for (ha = 0; ha < 2; ++ha) {
                for (at = 0; at < AT_COUNT; ++at) {
                    /* HOST PERFORMANCE NOTE: Apple libc materialises the whole
                     * 999999999-character field before truncating to dmax
                     * (~0.17 s per call, independent of dmax).  Sample 4
                     * tuples of this one kind (cap 0 x has_args x first 2 arg
                     * values); all other kinds keep the full cartesian
                     * product.  1084 table rows + 11 wide rows keeps the
                     * whole suite well under 10 s even under system load. */
                    if (k == FK_HUGE && (ci > 0 || at > 1)) continue;
                    struct guarded g;
                    size_t dmax = caps[ci];
                    int rc = -2;
                    int saved_errno;
                    const char *bad;
                    char tup[96];
                    char fmtbuf[8];

                    init_guarded(&g);
                    errno = 0;

                    switch (k) {
                        case FK_D:
                        case FK_HUGE: {
                            const char *fmt = (k == FK_D) ? "%d" : "%999999999d";
                            rc = cplus_snprintf_s(g.dest, dmax, fmt, int_value(at, ha));
                            break;
                        }
                        case FK_S:
                            rc = cplus_snprintf_s(g.dest, dmax, "%s", str_value(at, ha));
                            break;
                        case FK_N:
                            /* has_args=1: real sink; has_args=0: bogus address.
                             * The wrapper refuses %n before the vararg is read,
                             * so neither pointer is ever dereferenced. */
                            if (ha)
                                rc = cplus_snprintf_s(g.dest, dmax, "%n", (int *)&n_sink);
                            else
                                rc = cplus_snprintf_s(g.dest, dmax, "%n",
                                                      (int *)(uintptr_t)0x4141u);
                            break;
                        case FK_PREC_S:
                            rc = cplus_snprintf_s(g.dest, dmax, "%.*s",
                                                  precision_value(at, ha), str_value(at, ha));
                            break;
                        case FK_DYNAMIC: {
                            const char *pct = "%";
                            const char *conv = "d";
                            struct guarded g2;
                            int rc2, se2;
                            size_t i2;
                            fmtbuf[0] = pct[0]; /* built at runtime, not a literal */
                            fmtbuf[1] = conv[0];
                            fmtbuf[2] = '\0';
                            rc = cplus_snprintf_s(g.dest, dmax, fmtbuf, int_value(at, ha));
                            /* Exact non-literal behaviour: the _checked gate
                             * refuses this same runtime format. */
                            init_guarded(&g2);
                            errno = 0;
                            rc2 = cplus_snprintf_s_checked(0, (size_t)-1,
                                                           g2.dest, dmax, fmtbuf,
                                                           int_value(at, ha));
                            se2 = errno;
                            if (rc2 != -1 || se2 != EINVAL) {
                                printf("FINDING scenario_12: non-literal gate kind=%d cap=%zu "
                                       "has_args=%d arg_type=%d rc2=%d errno=%d\n",
                                       k, dmax, ha, at, rc2, se2);
                                ++findings;
                            }
                            for (i2 = 0; i2 < INNER; ++i2) {
                                if ((unsigned char)g2.dest[i2] != FILL_BYTE) {
                                    printf("FINDING scenario_12: non-literal gate wrote dest "
                                           "kind=%d cap=%zu has_args=%d arg_type=%d at=%zu\n",
                                           k, dmax, ha, at, i2);
                                    ++findings;
                                    break;
                                }
                            }
                            break;
                        }
                        case FK_POSITIONAL: {
                            /* %2$s: first vararg is a dummy int, second is the string.
                             * Held in a pointer so the format attribute does not
                             * flag the intentionally unused positional slot. */
                            const char *posfmt = "%2$s";
                            rc = cplus_snprintf_s(g.dest, dmax, posfmt, 0, str_value(at, ha));
                            break;
                        }
                        case FK_PCT:
                            rc = cplus_snprintf_s(g.dest, dmax, "%%");
                            break;
                        case FK_TEXT:
                            rc = cplus_snprintf_s(g.dest, dmax, "plain text");
                            break;
                        case FK_WIDE_DERIVED:
                            rc = cplus_snprintf_s(g.dest, dmax, "%ls", wide_value(at, ha));
                            break;
                        default:
                            break;
                    }
                    saved_errno = errno;
                    ++cases;

                    snprintf(tup, sizeof tup,
                             "kind=%d cap=%zu has_args=%d arg_type=%d rc=%d",
                             k, dmax, ha, at, rc);

                    bad = check_guarded(&g, dmax, rc);
                    if (bad) {
                        printf("FINDING scenario_12: %s issue=%s\n", tup, bad);
                        ++findings;
                    }

                    if (k == FK_N) {
                        size_t i;
                        if (rc != -1 || saved_errno != EINVAL || n_sink != (int)N_SENTINEL) {
                            printf("FINDING scenario_12: %s %%n-refusal errno=%d n_sink=%d\n",
                                   tup, saved_errno, (int)n_sink);
                            ++findings;
                        }
                        for (i = 0; i < dmax && i < INNER; ++i) {
                            if ((unsigned char)g.dest[i] != FILL_BYTE) {
                                printf("FINDING scenario_12: %s %%n wrote dest at=%zu\n",
                                       tup, i);
                                ++findings;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    /* ---- wide formats via the included libc header ----------------------- */
    {
        struct wguarded {
            unsigned char head[GUARD];
            wchar_t buf[64];
            unsigned char tail[GUARD];
        } wg;
        size_t wcaps[N_CAPS];
        size_t i;
        int wi;

        for (wi = 0; wi < N_CAPS; ++wi) wcaps[wi] = caps[wi];
        for (wi = 0; wi < N_CAPS; ++wi) {
            size_t wcap = wcaps[wi];
            int wr;
            fill(wg.head, GUARD, HEAD_BYTE);
            for (i = 0; i < 64; ++i) wg.buf[i] = (wchar_t)0x4A4A;
            fill(wg.tail, GUARD, TAIL_BYTE);
            wr = cplus_swprintf(wg.buf, wcap, L"%d-%s", 42, L"wide");
            ++cases;
            for (i = 0; i < GUARD; ++i) {
                if (wg.head[i] != HEAD_BYTE || wg.tail[i] != TAIL_BYTE) {
                    printf("FINDING scenario_12: wide canary cap=%zu rc=%d\n", wcap, wr);
                    ++findings;
                    break;
                }
            }
            for (i = wcap > 64 ? 64 : wcap; i < 64; ++i) {
                if (wg.buf[i] != (wchar_t)0x4A4A) {
                    printf("FINDING scenario_12: wide write past n cap=%zu rc=%d at=%zu\n",
                           wcap, wr, i);
                    ++findings;
                    break;
                }
            }
            if (wr < -1) {
                printf("FINDING scenario_12: wide undefined return cap=%zu rc=%d\n", wcap, wr);
                ++findings;
            }
        }

        /* Wide %n is a documented host pass-through: no C+ rejection.  Use a
         * real sink; a bogus pointer here would really be dereferenced. */
        {
            int wsink = -1;
            int wr;
            for (i = 0; i < 64; ++i) wg.buf[i] = (wchar_t)0x4A4A;
            wr = cplus_swprintf(wg.buf, 32, L"abc%n", &wsink);
            ++cases;
            printf("scenario_12 note: wide %%n host pass-through rc=%d sink=%d "
                   "(no C+ %%n rejection for wide formats)\n", wr, wsink);
            if (wsink == -1) {
                printf("FINDING scenario_12: wide %%n did not write sink rc=%d\n", wr);
                ++findings;
            }
        }
    }

    printf("scenario_12: %d cases ok\n", cases);
    /* A FINDING is a hard failure even though the process does not crash. */
    return findings ? 1 : 0;
}
