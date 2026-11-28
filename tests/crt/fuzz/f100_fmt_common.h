#ifndef F100_FMT_COMMON_H
#define F100_FMT_COMMON_H
/*
 * Shared helpers for deterministic format-API fuzz configs F-041..F-050.
 *
 * Discipline: every format string used by a harness is chosen from the fixed
 * table below (or is a literal).  No fuzzer/LCG byte is ever used as a format
 * string, so there is no attacker-controlled %-format (genuine UB / CWE-134).
 * Only the *arguments* and the truncation size are LCG-selected.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include "safe_format.h"
#include "safe_log.h"
#include "safe_runtime.h"

/* Numerical Recipes LCG; uint32_t arithmetic wraps mod 2^32 on every host. */
#define F100_LCG_MUL UINT32_C(1664525)
#define F100_LCG_ADD UINT32_C(1013904223)
#define F100_LCG_SEED UINT32_C(0xF100C0DE)

static uint32_t g_f100_state = F100_LCG_SEED;
static unsigned long g_f100_ops;
static unsigned long g_f100_fail;

static void f100_seed(uint32_t s) { g_f100_state = s; }
static uint32_t f100_next(void) {
    g_f100_state = g_f100_state * F100_LCG_MUL + F100_LCG_ADD;
    return g_f100_state;
}
static unsigned f100_below(unsigned n) {
    return n ? (unsigned)(f100_next() % (uint32_t)n) : 0u;
}
static void f100_init(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    /* Negative contract cases inspect the -1/EINVAL return path; with the
     * default violation trap they would _exit(70) before we could assert. */
    cplus_runtime_set_violation_trap(0);
    f100_seed(F100_LCG_SEED);
    g_f100_ops = 0;
    g_f100_fail = 0;
}
#define F100_OP() ((void)++g_f100_ops)
#define F100_FAIL(msg) do { \
    ++g_f100_fail; \
    fprintf(stderr, "F100 FAIL %s:%d %s\n", __FILE__, __LINE__, (msg)); \
} while (0)
static int f100_report(const char *cfg) {
    printf("CONFIG %s ops=%lu fail=%lu\n", cfg, g_f100_ops, g_f100_fail);
    return g_f100_fail == 0 ? 0 : 1;
}
/* Deterministic temp path for harnesses that redirect a standard stream. */
static void f100_tmp_path(char *out, size_t n, const char *tag) {
    const char *d = getenv("TMPDIR");
    if (d == NULL || *d == '\0') d = "/tmp";
    (void)snprintf(out, n, "%s/f100_fmt_%s_%ld.tmp", d, tag, (long)getpid());
}

/* Guard bytes around a 520-byte destination (largest dmax exercised is 512). */
#define F100_CANARY 0xA5u
typedef struct {
    unsigned char pre[16];
    char buf[520];              /* char: passed directly to the char* APIs */
    unsigned char post[16];
} f100_bbuf;
static void f100_bbuf_init(f100_bbuf *b) { memset(b, F100_CANARY, sizeof *b); }
static int f100_bbuf_ok(const f100_bbuf *b) {
    size_t i;
    for (i = 0; i < sizeof b->pre; ++i) if (b->pre[i] != F100_CANARY) return 0;
    for (i = 0; i < sizeof b->post; ++i) if (b->post[i] != F100_CANARY) return 0;
    return 1;
}

/* Fixed, type-matched conversion catalogue.  kind tells the dispatcher which
 * C type to pass through the varargs for that format. */
enum f100_kind {
    F100_K_INT = 0, F100_K_UINT, F100_K_LL, F100_K_ULL,
    F100_K_SIZE, F100_K_DBL, F100_K_STR
};
typedef struct { const char *fmt; enum f100_kind kind; } f100_spec;

static const f100_spec f100_specs[] = {
    { "%d",        F100_K_INT  }, { "%i",          F100_K_INT  },
    { "%+d",       F100_K_INT  }, { "%05d",        F100_K_INT  },
    { "%-12d|",    F100_K_INT  }, { "%.8d",        F100_K_INT  },
    { "%u",        F100_K_UINT }, { "%08u",        F100_K_UINT },
    { "%#x",       F100_K_UINT }, { "%08X",        F100_K_UINT },
    { "%lld",      F100_K_LL   }, { "%+lld",       F100_K_LL   },
    { "%llu",      F100_K_ULL  }, { "%#018llx",    F100_K_ULL  },
    { "%zu",       F100_K_SIZE }, { "%10zu",       F100_K_SIZE },
    { "%f",        F100_K_DBL  }, { "%.3f",        F100_K_DBL  },
    { "%+.2e",     F100_K_DBL  }, { "%g",          F100_K_DBL  },
    { "%#16.4g",   F100_K_DBL  }, { "%a",          F100_K_DBL  },
    { "%s",        F100_K_STR  }, { "%.16s",       F100_K_STR  },
    { "%-24.8s",   F100_K_STR  }
};
#define F100_NSPEC ((unsigned)(sizeof f100_specs / sizeof f100_specs[0]))

static const int f100_int_vals[] = {
    INT_MIN, -100000, -1, 0, 1, 42, 100000, INT_MAX
};
#define F100_NINT ((unsigned)(sizeof f100_int_vals / sizeof f100_int_vals[0]))
static const unsigned f100_uint_vals[] = { 0u, 1u, 7u, 65535u, UINT_MAX };
#define F100_NUINT ((unsigned)(sizeof f100_uint_vals / sizeof f100_uint_vals[0]))
static const long long f100_ll_vals[] = { LLONG_MIN, -1LL, 0LL, 1LL, LLONG_MAX };
#define F100_NLL ((unsigned)(sizeof f100_ll_vals / sizeof f100_ll_vals[0]))
static const unsigned long long f100_ull_vals[] = { 0ULL, 1ULL, ULLONG_MAX };
#define F100_NULLV ((unsigned)(sizeof f100_ull_vals / sizeof f100_ull_vals[0]))
static const size_t f100_size_vals[] = { 0u, 1u, 512u, SIZE_MAX };
#define F100_NSIZE ((unsigned)(sizeof f100_size_vals / sizeof f100_size_vals[0]))
static const double f100_dbl_vals[] = {
    0.0, -0.0, 1.0, -1.0, 0.5, 1e308, DBL_MIN, DBL_MAX, DBL_EPSILON
};
#define F100_NDBL ((unsigned)(sizeof f100_dbl_vals / sizeof f100_dbl_vals[0]))
static const char *f100_str_vals[] = {
    "", "a", "hello world", "0123456789abcdef0123456789abcdef", "x"
};
#define F100_NSTR ((unsigned)(sizeof f100_str_vals / sizeof f100_str_vals[0]))

/* Direct (varargs) call with a type-matched argument. */
static int f100_call_sn(char *dest, size_t dmax, const f100_spec *sp, unsigned vi) {
    switch (sp->kind) {
    case F100_K_INT:  return cplus_snprintf_s(dest, dmax, sp->fmt,
                                f100_int_vals[vi % F100_NINT]);
    case F100_K_UINT: return cplus_snprintf_s(dest, dmax, sp->fmt,
                                f100_uint_vals[vi % F100_NUINT]);
    case F100_K_LL:   return cplus_snprintf_s(dest, dmax, sp->fmt,
                                f100_ll_vals[vi % F100_NLL]);
    case F100_K_ULL:  return cplus_snprintf_s(dest, dmax, sp->fmt,
                                f100_ull_vals[vi % F100_NULLV]);
    case F100_K_SIZE: return cplus_snprintf_s(dest, dmax, sp->fmt,
                                f100_size_vals[vi % F100_NSIZE]);
    case F100_K_DBL:  return cplus_snprintf_s(dest, dmax, sp->fmt,
                                f100_dbl_vals[vi % F100_NDBL]);
    case F100_K_STR:  return cplus_snprintf_s(dest, dmax, sp->fmt,
                                f100_str_vals[vi % F100_NSTR]);
    }
    return -1;
}

/* va_list wrappers: the format is always one of the fixed table entries. */
static int f100_vsn_impl(char *dest, size_t dmax, const char *fmt, ...) {
    va_list ap;
    int rc;
    va_start(ap, fmt);
    rc = cplus_vsnprintf_s(dest, dmax, fmt, ap);
    va_end(ap);
    return rc;
}
static int f100_call_vsn(char *dest, size_t dmax, const f100_spec *sp, unsigned vi) {
    switch (sp->kind) {
    case F100_K_INT:  return f100_vsn_impl(dest, dmax, sp->fmt,
                                f100_int_vals[vi % F100_NINT]);
    case F100_K_UINT: return f100_vsn_impl(dest, dmax, sp->fmt,
                                f100_uint_vals[vi % F100_NUINT]);
    case F100_K_LL:   return f100_vsn_impl(dest, dmax, sp->fmt,
                                f100_ll_vals[vi % F100_NLL]);
    case F100_K_ULL:  return f100_vsn_impl(dest, dmax, sp->fmt,
                                f100_ull_vals[vi % F100_NULLV]);
    case F100_K_SIZE: return f100_vsn_impl(dest, dmax, sp->fmt,
                                f100_size_vals[vi % F100_NSIZE]);
    case F100_K_DBL:  return f100_vsn_impl(dest, dmax, sp->fmt,
                                f100_dbl_vals[vi % F100_NDBL]);
    case F100_K_STR:  return f100_vsn_impl(dest, dmax, sp->fmt,
                                f100_str_vals[vi % F100_NSTR]);
    }
    return -1;
}
/* Stream-output dispatchers (printf_s / fprintf_s). */
static int f100_call_printf(const f100_spec *sp, unsigned vi) {
    switch (sp->kind) {
    case F100_K_INT:  return cplus_printf_s(sp->fmt, f100_int_vals[vi % F100_NINT]);
    case F100_K_UINT: return cplus_printf_s(sp->fmt, f100_uint_vals[vi % F100_NUINT]);
    case F100_K_LL:   return cplus_printf_s(sp->fmt, f100_ll_vals[vi % F100_NLL]);
    case F100_K_ULL:  return cplus_printf_s(sp->fmt, f100_ull_vals[vi % F100_NULLV]);
    case F100_K_SIZE: return cplus_printf_s(sp->fmt, f100_size_vals[vi % F100_NSIZE]);
    case F100_K_DBL:  return cplus_printf_s(sp->fmt, f100_dbl_vals[vi % F100_NDBL]);
    case F100_K_STR:  return cplus_printf_s(sp->fmt, f100_str_vals[vi % F100_NSTR]);
    }
    return -1;
}
static int f100_call_fprintf(FILE *stream, const f100_spec *sp, unsigned vi) {
    switch (sp->kind) {
    case F100_K_INT:  return cplus_fprintf_s(stream, sp->fmt, f100_int_vals[vi % F100_NINT]);
    case F100_K_UINT: return cplus_fprintf_s(stream, sp->fmt, f100_uint_vals[vi % F100_NUINT]);
    case F100_K_LL:   return cplus_fprintf_s(stream, sp->fmt, f100_ll_vals[vi % F100_NLL]);
    case F100_K_ULL:  return cplus_fprintf_s(stream, sp->fmt, f100_ull_vals[vi % F100_NULLV]);
    case F100_K_SIZE: return cplus_fprintf_s(stream, sp->fmt, f100_size_vals[vi % F100_NSIZE]);
    case F100_K_DBL:  return cplus_fprintf_s(stream, sp->fmt, f100_dbl_vals[vi % F100_NDBL]);
    case F100_K_STR:  return cplus_fprintf_s(stream, sp->fmt, f100_str_vals[vi % F100_NSTR]);
    }
    return -1;
}
static int f100_vsp_impl(char *dest, size_t dmax, const char *fmt, ...) {
    va_list ap;
    int rc;
    va_start(ap, fmt);
    rc = cplus_vsprintf_s(dest, dmax, fmt, ap);
    va_end(ap);
    return rc;
}
static int f100_call_vsp(char *dest, size_t dmax, const f100_spec *sp, unsigned vi) {
    switch (sp->kind) {
    case F100_K_INT:  return f100_vsp_impl(dest, dmax, sp->fmt,
                                f100_int_vals[vi % F100_NINT]);
    case F100_K_UINT: return f100_vsp_impl(dest, dmax, sp->fmt,
                                f100_uint_vals[vi % F100_NUINT]);
    case F100_K_LL:   return f100_vsp_impl(dest, dmax, sp->fmt,
                                f100_ll_vals[vi % F100_NLL]);
    case F100_K_ULL:  return f100_vsp_impl(dest, dmax, sp->fmt,
                                f100_ull_vals[vi % F100_NULLV]);
    case F100_K_SIZE: return f100_vsp_impl(dest, dmax, sp->fmt,
                                f100_size_vals[vi % F100_NSIZE]);
    case F100_K_DBL:  return f100_vsp_impl(dest, dmax, sp->fmt,
                                f100_dbl_vals[vi % F100_NDBL]);
    case F100_K_STR:  return f100_vsp_impl(dest, dmax, sp->fmt,
                                f100_str_vals[vi % F100_NSTR]);
    }
    return -1;
}
#endif /* F100_FMT_COMMON_H */
