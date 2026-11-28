/* adv1000_fmt_io.c -- 1000 deterministic adversarial scenarios for the
 * checked C+ format and stream-I/O APIs (safecrt archive).
 *
 * Discipline (see NOTES.md):
 *  - Fixed-seed LCG (Numerical Recipes), every scenario consumes the same six
 *    draws -> fully deterministic and reproducible.
 *  - Format strings are NEVER built from random bytes. They come from a fixed
 *    safe catalogue (no exploitable %n). A separate tiny "forbidden" catalogue
 *    containing %n is passed only to APIs that must REFUSE it before any
 *    conversion runs (the SafeCRT violation trap is disabled so the -1 return
 *    path can be observed). No real %n write ever occurs.
 *  - Every scenario is canary-guarded (16 bytes before and after the data
 *    region) and checks a bounded/REFUSED result plus canary integrity.
 *  - Files use only TMPDIR (or /tmp) plus getpid(); no user paths.
 *
 * Output: one line per scenario  "S <id> <api> <case> <result> <canary_ok>"
 * then one summary line          "ADV1000 fmt_io 1000 <refused> <canary_ok> <failures>"
 * Exit is nonzero on any contract failure or any canary break.
 */
#define _POSIX_C_SOURCE 200112L
#include "safe_format.h"
#include "safe_io.h"
#include "safe_log.h"
#include "safe_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define NSCEN 1000
#define PAYLOAD_MAX 4096u
#define CANARY 0x5Au

/* ---------------- deterministic PRNG ---------------- */
#define LCG_MUL UINT32_C(1664525)
#define LCG_ADD UINT32_C(1013904223)
#define LCG_SEED UINT32_C(0xAD1000F1)
static uint32_t g_state = LCG_SEED;
static uint32_t rnd(void) { g_state = g_state * LCG_MUL + LCG_ADD; return g_state; }

/* ---------------- canary buffers ---------------- */
typedef struct {
    unsigned char pre[16];
    unsigned char buf[PAYLOAD_MAX];
    unsigned char post[16];
} adv_buf;
typedef struct { unsigned char data[16]; unsigned char guard[16]; } adv_ov;

static void b_init(adv_buf *b) { memset(b, CANARY, sizeof *b); }
static void ov_init(adv_ov *o) { memset(o, CANARY, sizeof *o); }
static int region_ok(const unsigned char *p, size_t n) {
    size_t i; for (i = 0; i < n; ++i) if (p[i] != CANARY) return 0; return 1;
}
static int b_canary(const adv_buf *b) {
    return region_ok(b->pre, sizeof b->pre) && region_ok(b->post, sizeof b->post);
}
static int ov_canary(const adv_ov *o) { return region_ok(o->guard, sizeof o->guard); }

/* ---------------- fixed safe format catalogue ---------------- */
enum fk { K_INT = 0, K_UINT, K_LL, K_ULL, K_SIZE, K_DBL, K_STR };
typedef struct { const char *fmt; enum fk kind; } fspec;
static const fspec SPECS[] = {
    { "%d",        K_INT  }, { "%+08d",    K_INT  }, { "%-12d|", K_INT  },
    { "%.8d",      K_INT  }, { "%u",       K_UINT }, { "%08X",    K_UINT },
    { "%#x",       K_UINT }, { "%lld",     K_LL   }, { "%+lld",   K_LL   },
    { "%llu",      K_ULL  }, { "%#018llx", K_ULL  }, { "%zu",     K_SIZE },
    { "%10zu",     K_SIZE }, { "%.3f",     K_DBL  }, { "%+.2e",   K_DBL  },
    { "%#16.4g",   K_DBL  }, { "%s",       K_STR  }, { "%.16s",   K_STR  },
    { "%-24.8s",   K_STR  },
};
#define NSPEC ((int)(sizeof SPECS / sizeof SPECS[0]))
static const char *FORBIDDEN[] = { "%n", "%hhn", "%lln", "AB%n", "%d %n", "%.3s%n" };
#define NFORBID ((int)(sizeof FORBIDDEN / sizeof FORBIDDEN[0]))
static const char *SCAN_WFMT[] = { "%7s", "%15s", "%31s", "%63s", "%127s", "%255s" };
static const size_t SCAN_WVAL[] = { 7u, 15u, 31u, 63u, 127u, 255u };
#define NSW ((int)(sizeof SCAN_WFMT / sizeof SCAN_WFMT[0]))

/* ---------------- deterministic value pools ---------------- */
static const int V_INT[] = { INT_MIN, -100000, -1, 0, 1, 42, 100000, INT_MAX };
static const unsigned V_UINT[] = { 0u, 1u, 7u, 65535u, UINT_MAX };
static const long long V_LL[] = { LLONG_MIN, -1LL, 0LL, 1LL, LLONG_MAX };
static const unsigned long long V_ULL[] = { 0ULL, 1ULL, ULLONG_MAX };
static const size_t V_SIZE[] = { 0u, 1u, 512u, SIZE_MAX };
static const double V_DBL[] = { 0.0, -0.0, 1.0, -1.0, 0.5, 1e308, 2.2250738585072014e-308, 1.7976931348623157e308 };
static const char *V_STR[] = { "", "a", "hello world", "0123456789abcdef0123456789abcdef", "x" };
#define NEL(a) ((unsigned)(sizeof(a) / sizeof((a)[0])))

static char g_payload[PAYLOAD_MAX + 1];
static void fill_payload(size_t n) {
    size_t i; for (i = 0; i < n; ++i) g_payload[i] = (char)('A' + (i % 26)); g_payload[n] = '\0';
}

/* ---------------- counters / emit ---------------- */
static unsigned long g_refused, g_canary_ok, g_fail;

static void emit(int id, const char *api, const char *cas, const char *result,
                 int canary_ok, int fail) {
    if (canary_ok) ++g_canary_ok; else ++fail;
    if (strcmp(result, "REFUSED") == 0) ++g_refused;
    if (fail) ++g_fail;
    printf("S %d %s %s %s %d\n", id, api, cas, result, canary_ok ? 1 : 0);
}

/* ---------------- type-matched varargs dispatchers ---------------- */
static int call_sn(char *d, size_t n, const fspec *sp, unsigned v) {
    switch (sp->kind) {
    case K_INT:  return cplus_snprintf_s(d, n, sp->fmt, V_INT[v % NEL(V_INT)]);
    case K_UINT: return cplus_snprintf_s(d, n, sp->fmt, V_UINT[v % NEL(V_UINT)]);
    case K_LL:   return cplus_snprintf_s(d, n, sp->fmt, V_LL[v % NEL(V_LL)]);
    case K_ULL:  return cplus_snprintf_s(d, n, sp->fmt, V_ULL[v % NEL(V_ULL)]);
    case K_SIZE: return cplus_snprintf_s(d, n, sp->fmt, V_SIZE[v % NEL(V_SIZE)]);
    case K_DBL:  return cplus_snprintf_s(d, n, sp->fmt, V_DBL[v % NEL(V_DBL)]);
    case K_STR:  return cplus_snprintf_s(d, n, sp->fmt, V_STR[v % NEL(V_STR)]);
    }
    return -1;
}
static int call_sp(char *d, size_t n, const fspec *sp, unsigned v) {
    switch (sp->kind) {
    case K_INT:  return cplus_sprintf_s(d, n, sp->fmt, V_INT[v % NEL(V_INT)]);
    case K_UINT: return cplus_sprintf_s(d, n, sp->fmt, V_UINT[v % NEL(V_UINT)]);
    case K_LL:   return cplus_sprintf_s(d, n, sp->fmt, V_LL[v % NEL(V_LL)]);
    case K_ULL:  return cplus_sprintf_s(d, n, sp->fmt, V_ULL[v % NEL(V_ULL)]);
    case K_SIZE: return cplus_sprintf_s(d, n, sp->fmt, V_SIZE[v % NEL(V_SIZE)]);
    case K_DBL:  return cplus_sprintf_s(d, n, sp->fmt, V_DBL[v % NEL(V_DBL)]);
    case K_STR:  return cplus_sprintf_s(d, n, sp->fmt, V_STR[v % NEL(V_STR)]);
    }
    return -1;
}
static int call_sn_checked(int lit, size_t known, char *d, size_t n, const fspec *sp, unsigned v) {
    switch (sp->kind) {
    case K_INT:  return cplus_snprintf_s_checked(lit, known, d, n, sp->fmt, V_INT[v % NEL(V_INT)]);
    case K_UINT: return cplus_snprintf_s_checked(lit, known, d, n, sp->fmt, V_UINT[v % NEL(V_UINT)]);
    case K_LL:   return cplus_snprintf_s_checked(lit, known, d, n, sp->fmt, V_LL[v % NEL(V_LL)]);
    case K_ULL:  return cplus_snprintf_s_checked(lit, known, d, n, sp->fmt, V_ULL[v % NEL(V_ULL)]);
    case K_SIZE: return cplus_snprintf_s_checked(lit, known, d, n, sp->fmt, V_SIZE[v % NEL(V_SIZE)]);
    case K_DBL:  return cplus_snprintf_s_checked(lit, known, d, n, sp->fmt, V_DBL[v % NEL(V_DBL)]);
    case K_STR:  return cplus_snprintf_s_checked(lit, known, d, n, sp->fmt, V_STR[v % NEL(V_STR)]);
    }
    return -1;
}
static int call_sp_checked(int lit, size_t known, char *d, size_t n, const fspec *sp, unsigned v) {
    switch (sp->kind) {
    case K_INT:  return cplus_sprintf_s_checked(lit, known, d, n, sp->fmt, V_INT[v % NEL(V_INT)]);
    case K_UINT: return cplus_sprintf_s_checked(lit, known, d, n, sp->fmt, V_UINT[v % NEL(V_UINT)]);
    case K_LL:   return cplus_sprintf_s_checked(lit, known, d, n, sp->fmt, V_LL[v % NEL(V_LL)]);
    case K_ULL:  return cplus_sprintf_s_checked(lit, known, d, n, sp->fmt, V_ULL[v % NEL(V_ULL)]);
    case K_SIZE: return cplus_sprintf_s_checked(lit, known, d, n, sp->fmt, V_SIZE[v % NEL(V_SIZE)]);
    case K_DBL:  return cplus_sprintf_s_checked(lit, known, d, n, sp->fmt, V_DBL[v % NEL(V_DBL)]);
    case K_STR:  return cplus_sprintf_s_checked(lit, known, d, n, sp->fmt, V_STR[v % NEL(V_STR)]);
    }
    return -1;
}
static int call_fprintf(FILE *s, const fspec *sp, unsigned v) {
    switch (sp->kind) {
    case K_INT:  return cplus_fprintf_s(s, sp->fmt, V_INT[v % NEL(V_INT)]);
    case K_UINT: return cplus_fprintf_s(s, sp->fmt, V_UINT[v % NEL(V_UINT)]);
    case K_LL:   return cplus_fprintf_s(s, sp->fmt, V_LL[v % NEL(V_LL)]);
    case K_ULL:  return cplus_fprintf_s(s, sp->fmt, V_ULL[v % NEL(V_ULL)]);
    case K_SIZE: return cplus_fprintf_s(s, sp->fmt, V_SIZE[v % NEL(V_SIZE)]);
    case K_DBL:  return cplus_fprintf_s(s, sp->fmt, V_DBL[v % NEL(V_DBL)]);
    case K_STR:  return cplus_fprintf_s(s, sp->fmt, V_STR[v % NEL(V_STR)]);
    }
    return -1;
}
static int call_fprintf_checked(int lit, FILE *s, const fspec *sp, unsigned v) {
    switch (sp->kind) {
    case K_INT:  return cplus_fprintf_s_checked(lit, s, sp->fmt, V_INT[v % NEL(V_INT)]);
    case K_UINT: return cplus_fprintf_s_checked(lit, s, sp->fmt, V_UINT[v % NEL(V_UINT)]);
    case K_LL:   return cplus_fprintf_s_checked(lit, s, sp->fmt, V_LL[v % NEL(V_LL)]);
    case K_ULL:  return cplus_fprintf_s_checked(lit, s, sp->fmt, V_ULL[v % NEL(V_ULL)]);
    case K_SIZE: return cplus_fprintf_s_checked(lit, s, sp->fmt, V_SIZE[v % NEL(V_SIZE)]);
    case K_DBL:  return cplus_fprintf_s_checked(lit, s, sp->fmt, V_DBL[v % NEL(V_DBL)]);
    case K_STR:  return cplus_fprintf_s_checked(lit, s, sp->fmt, V_STR[v % NEL(V_STR)]);
    }
    return -1;
}
/* printf_s("%.*s") with stdout redirected to tf; verifies bounded output. */
static int call_printf_len(const char *payload, int len, FILE *tf) {
    int rc = -1;
    fflush(stdout);
    {
        int saved = dup(STDOUT_FILENO);
        if (saved < 0) return -1;
        if (dup2(fileno(tf), STDOUT_FILENO) >= 0) {
            rc = cplus_printf_s("%.*s", len, payload);
            fflush(stdout);
            dup2(saved, STDOUT_FILENO);
        }
        close(saved);
    }
    return rc;
}

/* ---------------- scenario catalogue ---------------- */
enum {
    C_SN_TRUNC, C_SN_OUTLEN, C_SN_DMAX0, C_SN_NULLDEST, C_SN_ONE_OVER, C_SN_NONLIT, C_SN_PCTN,
    C_SP_TRUNC, C_SP_OUTLEN, C_SP_NULLDEST, C_SP_ONE_OVER, C_SP_NONLIT, C_SP_PCTN,
    C_PR_OUTLEN, C_PR_PCTN, C_PR_NONLIT, C_PR_NULLFMT,
    C_FP_NULLSTREAM, C_FP_OUTLEN, C_FP_PCTN, C_FP_NONLIT, C_FP_NULLFMT,
    C_SS_WIDTH, C_SS_NULLIN, C_SS_UNBOUNDED, C_SS_PCTN, C_SS_NONLIT,
    C_FS_NULLSTREAM, C_FS_WIDTH, C_FS_UNBOUNDED, C_FS_PCTN, C_FS_NONLIT,
    C_FO_OPEN, C_FO_NULL,
    C_FR_NULLBUF, C_FR_NULLSTREAM, C_FR_ZERO, C_FR_ONE_OVER, C_FR_BINRT,
    C_FW_NULLBUF, C_FW_NULLSTREAM, C_FW_ZERO, C_FW_ONE_OVER, C_FW_BINRT,
    C_FC_DOUBLE, C_FC_NULL,
    NCASES
};
typedef struct { const char *api; const char *name; } case_info;
static const case_info CI[NCASES] = {
    {"snprintf_s","TRUNC"},      {"snprintf_s","OUTLEN"},    {"snprintf_s","DMAX0"},
    {"snprintf_s","NULL_DEST"},  {"snprintf_s","ONE_OVER"},  {"snprintf_s","NONLITERAL"},
    {"snprintf_s","PCT_N"},
    {"sprintf_s","TRUNC"},       {"sprintf_s","OUTLEN"},     {"sprintf_s","NULL_DEST"},
    {"sprintf_s","ONE_OVER"},    {"sprintf_s","NONLITERAL"}, {"sprintf_s","PCT_N"},
    {"printf_s","OUTLEN"},       {"printf_s","PCT_N"},       {"printf_s","NONLITERAL"},
    {"printf_s","NULL_FMT"},
    {"fprintf_s","NULL_STREAM"}, {"fprintf_s","OUTLEN"},     {"fprintf_s","PCT_N"},
    {"fprintf_s","NONLITERAL"},  {"fprintf_s","NULL_FMT"},
    {"sscanf_s","SCAN_WIDTH"},   {"sscanf_s","NULL_INPUT"},  {"sscanf_s","UNBOUNDED"},
    {"sscanf_s","PCT_N"},        {"sscanf_s","NONLITERAL"},
    {"fscanf_s","NULL_STREAM"},  {"fscanf_s","SCAN_WIDTH"},  {"fscanf_s","UNBOUNDED"},
    {"fscanf_s","PCT_N"},        {"fscanf_s","NONLITERAL"},
    {"fopen_s","OPEN_OK"},       {"fopen_s","NULL_ARG"},
    {"fread_s","NULL_BUF"},      {"fread_s","NULL_STREAM"},  {"fread_s","ZERO"},
    {"fread_s","READ_ONE_OVER"}, {"fread_s","BIN_ROUNDTRIP"},
    {"fwrite_s","NULL_BUF"},     {"fwrite_s","NULL_STREAM"}, {"fwrite_s","ZERO"},
    {"fwrite_s","WRITE_ONE_OVER"},{"fwrite_s","BIN_ROUNDTRIP"},
    {"fclose_s","DOUBLE_CLOSE"}, {"fclose_s","NULL_STREAM"},
};

/* Deterministic per-case path in TMPDIR only. */
static void tmp_path(char *out, size_t n, int id) {
    const char *d = getenv("TMPDIR");
    if (d == NULL || *d == '\0') d = "/tmp";
    (void)snprintf(out, n, "%s/adv1000_fmtio_%ld_%d.tmp", d, (long)getpid(), id);
}

/* NUL-terminated-within-n check used by bounded string cases. */
static int terminated_within(const unsigned char *p, size_t n) {
    size_t i; for (i = 0; i < n; ++i) if (p[i] == 0) return 1; return 0;
}

int main(void) {
    int id;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_runtime_set_violation_trap(0); /* observe -1/EINVAL refusal returns */
    setvbuf(stdout, NULL, _IOLBF, 0);
    g_state = LCG_SEED;

    for (id = 1; id <= NSCEN; ++id) {
        uint32_t r0 = rnd(), r1 = rnd(), r2 = rnd(), r3 = rnd(), r4 = rnd(), r5 = rnd();
        int ci = (id - 1) % NCASES;
        const char *api = CI[ci].api, *cas = CI[ci].name;
        const char *result = "OK";
        int canary_ok = 1, fail = 0, rc;
        size_t dmax = (size_t)(r0 % 257u);        /* 0..256 */
        size_t outlen = (size_t)(r1 % 257u);      /* 0..256 */
        size_t cap = 8u + (size_t)(r2 % 249u);    /* 8..256 */
        unsigned v = r3;
        const fspec *sp = &SPECS[r4 % (uint32_t)NSPEC];
        adv_buf b; adv_ov ov;
        b_init(&b); ov_init(&ov);

        switch (ci) {
        /* ---------------- snprintf_s ---------------- */
        case C_SN_TRUNC: {
            /* Oversized format output into a small buffer: many conversion
             * bytes, tiny dmax -> bounded truncation, canary intact. */
            fill_payload(200u);
            rc = cplus_snprintf_s((char *)b.buf, dmax, "%.200s", g_payload);
            canary_ok = b_canary(&b);
            if (rc < 0 || !region_ok(b.buf + dmax, sizeof b.buf - dmax) ||
                (dmax > 0 && !terminated_within(b.buf, dmax))) fail = 1;
            result = (rc >= 0 && (size_t)rc >= dmax) ? "TRUNC" : "OK";
            break;
        }
        case C_SN_OUTLEN: {
            fill_payload(outlen);
            rc = cplus_snprintf_s((char *)b.buf, dmax, "%.*s", (int)outlen, g_payload);
            canary_ok = b_canary(&b);
            if (rc < 0 || !region_ok(b.buf + dmax, sizeof b.buf - dmax) ||
                (dmax > 0 && !terminated_within(b.buf, dmax))) fail = 1;
            result = (rc >= 0 && (size_t)rc >= dmax) ? "TRUNC" : "OK";
            break;
        }
        case C_SN_DMAX0: {
            rc = call_sn((char *)b.buf, 0u, sp, v);
            canary_ok = b_canary(&b);
            if (rc < 0 || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "OK"; /* zero capacity: nothing written */
            break;
        }
        case C_SN_NULLDEST: {
            errno = 0;
            rc = call_sn(NULL, (size_t)(1u + r5 % 32u), sp, v);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_SN_ONE_OVER: {
            /* known_dmax declares the real capacity; dmax is one over. */
            errno = 0;
            rc = call_sn_checked(1, cap, (char *)b.buf, cap + 1u, sp, v);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_SN_NONLIT: {
            errno = 0;
            rc = call_sn_checked(0, sizeof b.buf, (char *)b.buf, dmax ? dmax : 1u, sp, v);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_SN_PCTN: {
            const char *f = FORBIDDEN[r5 % (uint32_t)NFORBID];
            errno = 0;
            rc = cplus_snprintf_s((char *)b.buf, dmax ? dmax : 1u, f, 1);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "REFUSED";
            break;
        }

        /* ---------------- sprintf_s ---------------- */
        case C_SP_TRUNC: {
            fill_payload(200u);
            rc = cplus_sprintf_s((char *)b.buf, dmax, "%.200s", g_payload);
            canary_ok = b_canary(&b);
            if (rc < 0 || !region_ok(b.buf + dmax, sizeof b.buf - dmax) ||
                (dmax > 0 && !terminated_within(b.buf, dmax))) fail = 1;
            result = (rc >= 0 && (size_t)rc >= dmax) ? "TRUNC" : "OK";
            break;
        }
        case C_SP_OUTLEN: {
            fill_payload(outlen);
            rc = cplus_sprintf_s((char *)b.buf, dmax, "%.*s", (int)outlen, g_payload);
            canary_ok = b_canary(&b);
            if (rc < 0 || !region_ok(b.buf + dmax, sizeof b.buf - dmax) ||
                (dmax > 0 && !terminated_within(b.buf, dmax))) fail = 1;
            result = (rc >= 0 && (size_t)rc >= dmax) ? "TRUNC" : "OK";
            break;
        }
        case C_SP_NULLDEST: {
            errno = 0;
            rc = call_sp(NULL, (size_t)(1u + r5 % 32u), sp, v);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_SP_ONE_OVER: {
            errno = 0;
            rc = call_sp_checked(1, cap, (char *)b.buf, cap + 1u, sp, v);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_SP_NONLIT: {
            errno = 0;
            rc = call_sp_checked(0, sizeof b.buf, (char *)b.buf, dmax ? dmax : 1u, sp, v);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_SP_PCTN: {
            const char *f = FORBIDDEN[r5 % (uint32_t)NFORBID];
            errno = 0;
            rc = cplus_sprintf_s((char *)b.buf, dmax ? dmax : 1u, f, 1);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "REFUSED";
            break;
        }

        /* ---------------- printf_s ---------------- */
        case C_PR_OUTLEN: {
            FILE *tf = tmpfile();
            off_t pos;
            fill_payload(outlen);
            if (!tf) { fail = 1; break; }
            rc = call_printf_len(g_payload, (int)outlen, tf);
            pos = lseek(fileno(tf), 0, SEEK_CUR);
            canary_ok = b_canary(&b);
            if (rc < 0 || (size_t)pos > outlen || !canary_ok) fail = 1;
            result = "OK";
            fclose(tf);
            break;
        }
        case C_PR_PCTN: {
            const char *f = FORBIDDEN[r5 % (uint32_t)NFORBID];
            errno = 0;
            rc = cplus_printf_s(f, 1);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_PR_NONLIT: {
            errno = 0;
            rc = cplus_printf_s_checked(0, sp->fmt, 1);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_PR_NULLFMT: {
            errno = 0;
            rc = cplus_printf_s(NULL);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            break;
        }

        /* ---------------- fprintf_s ---------------- */
        case C_FP_NULLSTREAM: {
            errno = 0;
            rc = call_fprintf(NULL, sp, v); /* NULL stream write: must refuse */
            canary_ok = ov_canary(&ov) && b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_FP_OUTLEN: {
            FILE *tf = tmpfile();
            off_t pos;
            if (!tf) { fail = 1; break; }
            fill_payload(outlen);
            rc = cplus_fprintf_s(tf, "%.*s", (int)outlen, g_payload);
            fflush(tf);
            pos = lseek(fileno(tf), 0, SEEK_CUR);
            canary_ok = ov_canary(&ov) && b_canary(&b);
            if (rc < 0 || (size_t)pos > outlen || !canary_ok) fail = 1;
            result = "OK";
            fclose(tf);
            break;
        }
        case C_FP_PCTN: {
            FILE *tf = tmpfile();
            const char *f = FORBIDDEN[r5 % (uint32_t)NFORBID];
            errno = 0;
            if (!tf) { fail = 1; break; }
            rc = cplus_fprintf_s(tf, f, 1);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            fclose(tf);
            break;
        }
        case C_FP_NONLIT: {
            FILE *tf = tmpfile();
            errno = 0;
            if (!tf) { fail = 1; break; }
            rc = call_fprintf_checked(0, tf, sp, v);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            fclose(tf);
            break;
        }
        case C_FP_NULLFMT: {
            FILE *tf = tmpfile();
            errno = 0;
            if (!tf) { fail = 1; break; }
            rc = cplus_fprintf_s(tf, NULL);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            fclose(tf);
            break;
        }

        /* ---------------- sscanf_s ---------------- */
        case C_SS_WIDTH: {
            int wi = (int)(r5 % (uint32_t)NSW);
            size_t width = SCAN_WVAL[wi];
            const char *fmt = SCAN_WFMT[wi];
            fill_payload(outlen);
            rc = cplus_sscanf_s(g_payload, fmt, (char *)b.buf);
            canary_ok = b_canary(&b);
            if (!region_ok(b.buf + width + 1u, sizeof b.buf - width - 1u) || !canary_ok) fail = 1;
            result = (outlen > width) ? "TRUNC" : "OK";
            break;
        }
        case C_SS_NULLIN: {
            errno = 0;
            rc = cplus_sscanf_s(NULL, "%d", &rc);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_SS_UNBOUNDED: {
            fill_payload(outlen + 1u);
            errno = 0;
            rc = cplus_sscanf_s(g_payload, "%s", (char *)b.buf);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_SS_PCTN: {
            const char *f = FORBIDDEN[r5 % (uint32_t)NFORBID];
            errno = 0;
            rc = cplus_sscanf_s("1 2", f);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_SS_NONLIT: {
            const char *f = "%d";
            errno = 0;
            rc = cplus_sscanf_s_checked(0, "123", f, &rc);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            break;
        }

        /* ---------------- fscanf_s ---------------- */
        case C_FS_NULLSTREAM: {
            errno = 0;
            rc = cplus_fscanf_s(NULL, "%d", &rc);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_FS_WIDTH: {
            int wi = (int)(r5 % (uint32_t)NSW);
            size_t width = SCAN_WVAL[wi];
            const char *fmt = SCAN_WFMT[wi];
            FILE *tf = tmpfile();
            if (!tf) { fail = 1; break; }
            fill_payload(outlen);
            (void)fwrite(g_payload, 1, outlen, tf);
            rewind(tf);
            rc = cplus_fscanf_s(tf, fmt, (char *)b.buf);
            canary_ok = b_canary(&b);
            if (!region_ok(b.buf + width + 1u, sizeof b.buf - width - 1u) || !canary_ok) fail = 1;
            result = (outlen > width) ? "TRUNC" : "OK";
            fclose(tf);
            break;
        }
        case C_FS_UNBOUNDED: {
            FILE *tf = tmpfile();
            errno = 0;
            if (!tf) { fail = 1; break; }
            fputs("abc", tf); rewind(tf);
            rc = cplus_fscanf_s(tf, "%s", (char *)b.buf);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "REFUSED";
            fclose(tf);
            break;
        }
        case C_FS_PCTN: {
            FILE *tf = tmpfile();
            const char *f = FORBIDDEN[r5 % (uint32_t)NFORBID];
            errno = 0;
            if (!tf) { fail = 1; break; }
            fputs("1 2", tf); rewind(tf);
            rc = cplus_fscanf_s(tf, f);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            fclose(tf);
            break;
        }
        case C_FS_NONLIT: {
            FILE *tf = tmpfile();
            const char *f = "%d";
            errno = 0;
            if (!tf) { fail = 1; break; }
            fputs("123", tf); rewind(tf);
            rc = cplus_fscanf_s_checked(0, tf, f, &rc);
            canary_ok = b_canary(&b);
            if (rc >= 0 || errno != EINVAL) fail = 1;
            result = "REFUSED";
            fclose(tf);
            break;
        }

        /* ---------------- fopen_s ---------------- */
        case C_FO_OPEN: {
            char path[512]; FILE *f;
            tmp_path(path, sizeof path, id);
            f = cplus_fopen_s(path, "wb+");
            canary_ok = b_canary(&b);
            if (f == NULL) fail = 1;
            else if (cplus_fclose_s(f) != 0) fail = 1;
            (void)unlink(path);
            result = "OK";
            break;
        }
        case C_FO_NULL: {
            FILE *f;
            errno = 0;
            f = (r5 & 1u) ? cplus_fopen_s(NULL, "r") : cplus_fopen_s("/dev/null", NULL);
            canary_ok = b_canary(&b);
            if (f != NULL || errno != EINVAL) fail = 1;
            result = "REFUSED";
            break;
        }

        /* ---------------- fread_s ---------------- */
        case C_FR_NULLBUF: {
            FILE *tf = tmpfile(); size_t got;
            if (!tf) { fail = 1; break; }
            got = cplus_fread_s(NULL, sizeof b.buf, 1, 8u, tf);
            canary_ok = b_canary(&b);
            if (got != 0u) fail = 1;
            result = "REFUSED";
            fclose(tf);
            break;
        }
        case C_FR_NULLSTREAM: {
            size_t got = cplus_fread_s(b.buf, sizeof b.buf, 1, 8u, NULL);
            canary_ok = b_canary(&b);
            if (got != 0u || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_FR_ZERO: {
            FILE *tf = tmpfile(); size_t got;
            if (!tf) { fail = 1; break; }
            got = ((r5 & 1u) ? cplus_fread_s(b.buf, sizeof b.buf, 0u, 8u, tf)
                             : cplus_fread_s(b.buf, sizeof b.buf, 1u, 0u, tf));
            canary_ok = b_canary(&b);
            if (got != 0u || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "ZERO";
            fclose(tf);
            break;
        }
        case C_FR_ONE_OVER: {
            /* Source has 32 bytes; declared bmax=16, request count=17
             * (one over) -> must clamp to 16 and leave the guard intact. */
            FILE *tf = tmpfile(); size_t got;
            unsigned char src[32]; size_t i;
            if (!tf) { fail = 1; break; }
            for (i = 0; i < sizeof src; ++i) src[i] = (unsigned char)i;
            (void)fwrite(src, 1, sizeof src, tf); rewind(tf);
            ov_init(&ov);
            got = cplus_fread_s(ov.data, sizeof ov.data, 1, sizeof ov.data + 1u, tf);
            canary_ok = ov_canary(&ov);
            if (got != sizeof ov.data || !canary_ok || memcmp(ov.data, src, sizeof ov.data) != 0) fail = 1;
            result = "CLAMPED";
            fclose(tf);
            break;
        }
        case C_FR_BINRT: {
            size_t n = outlen; /* 0..256 in the roundtrip slice; larger in FW */
            FILE *tf = tmpfile();
            size_t got, srcn = (size_t)(r5 % (uint32_t)(PAYLOAD_MAX + 1u));
            if (!tf) { fail = 1; break; }
            (void)n;
            for (v = 0; v < srcn; ++v) g_payload[v] = (char)((v * 31u + 7u) & 0xFFu);
            got = cplus_fwrite_s(g_payload, srcn, 1, srcn, tf);
            if (got != srcn) fail = 1;
            rewind(tf);
            b_init(&b);
            got = cplus_fread_s(b.buf, srcn ? srcn : 1u, 1, srcn, tf);
            canary_ok = b_canary(&b);
            if (got != srcn || (srcn && memcmp(b.buf, g_payload, srcn) != 0) || !canary_ok) fail = 1;
            result = "OK";
            fclose(tf);
            break;
        }

        /* ---------------- fwrite_s ---------------- */
        case C_FW_NULLBUF: {
            FILE *tf = tmpfile(); size_t got;
            if (!tf) { fail = 1; break; }
            got = cplus_fwrite_s(NULL, sizeof b.buf, 1, 8u, tf);
            canary_ok = b_canary(&b);
            if (got != 0u) fail = 1;
            result = "REFUSED";
            fclose(tf);
            break;
        }
        case C_FW_NULLSTREAM: {
            size_t got = cplus_fwrite_s(b.buf, sizeof b.buf, 1, 8u, NULL);
            canary_ok = b_canary(&b);
            if (got != 0u || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "REFUSED";
            break;
        }
        case C_FW_ZERO: {
            FILE *tf = tmpfile(); size_t got;
            if (!tf) { fail = 1; break; }
            got = ((r5 & 1u) ? cplus_fwrite_s(b.buf, sizeof b.buf, 0u, 8u, tf)
                             : cplus_fwrite_s(b.buf, sizeof b.buf, 1u, 0u, tf));
            canary_ok = b_canary(&b);
            if (got != 0u || !region_ok(b.buf, sizeof b.buf)) fail = 1;
            result = "ZERO";
            fclose(tf);
            break;
        }
        case C_FW_ONE_OVER: {
            FILE *tf = tmpfile(); size_t got; size_t i;
            if (!tf) { fail = 1; break; }
            ov_init(&ov);
            for (i = 0; i < sizeof ov.data; ++i) ov.data[i] = (unsigned char)(0xA0u + i);
            got = cplus_fwrite_s(ov.data, sizeof ov.data, 1, sizeof ov.data + 1u, tf);
            canary_ok = ov_canary(&ov);
            if (got != sizeof ov.data || !canary_ok) fail = 1;
            result = "CLAMPED";
            fclose(tf);
            break;
        }
        case C_FW_BINRT: {
            /* Full 0..4096 byte deterministic binary roundtrip. */
            size_t srcn = (size_t)(r5 % (uint32_t)(PAYLOAD_MAX + 1u));
            FILE *tf = tmpfile();
            size_t got, i;
            if (!tf) { fail = 1; break; }
            for (i = 0; i < srcn; ++i) g_payload[i] = (char)((i * 73u + 19u) & 0xFFu);
            got = cplus_fwrite_s(g_payload, srcn, 1, srcn, tf);
            if (got != srcn) fail = 1;
            fflush(tf); rewind(tf);
            b_init(&b);
            got = cplus_fread_s(b.buf, srcn ? srcn : 1u, 1, srcn, tf);
            canary_ok = b_canary(&b);
            if (got != srcn || (srcn && memcmp(b.buf, g_payload, srcn) != 0) || !canary_ok) fail = 1;
            result = "OK";
            fclose(tf);
            break;
        }

        /* ---------------- fclose_s ---------------- */
        case C_FC_DOUBLE: {
            char path[512]; FILE *f;
            tmp_path(path, sizeof path, id);
            f = cplus_fopen_s(path, "wb+");
            if (f == NULL) { fail = 1; canary_ok = b_canary(&b); break; }
            if (cplus_fclose_s(f) != 0) fail = 1;
            errno = 0;
            rc = cplus_fclose_s(f); /* second close must be refused, no UAF */
            canary_ok = b_canary(&b);
            if (rc != EOF || errno != EINVAL) fail = 1;
            result = "REFUSED";
            (void)unlink(path);
            break;
        }
        case C_FC_NULL: {
            errno = 0;
            rc = cplus_fclose_s(NULL);
            canary_ok = b_canary(&b);
            if (rc != EOF || errno != EINVAL) fail = 1;
            result = "REFUSED";
            break;
        }
        default:
            fail = 1;
            break;
        }

        emit(id, api, cas, result, canary_ok, fail);
    }

    printf("ADV1000 fmt_io %d %lu %lu %lu\n", NSCEN, g_refused, g_canary_ok, g_fail);
    return (g_fail != 0u || g_canary_ok != (unsigned long)NSCEN) ? 1 : 0;
}
