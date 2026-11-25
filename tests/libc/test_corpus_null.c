/*
 * test_corpus_null.c -- deterministic adversarial NULL/invalid-argument corpus
 * for the C+ hardened libc (branch agent/modern-stdlib).
 *
 * Scope / ownership: this file is the only artifact produced by corpus agent 1.
 * Standalone C99; links ONLY build/libc/libcplus_hardened.a.
 *
 * Method
 * ------
 * Every libc/src implementation file was read to classify each public cplus_*
 * entry point that takes a pointer parameter:
 *
 *   GUARDED   -- source contains an explicit NULL test on that parameter before
 *                any dereference/forward, with a documented return + errno.
 *                These are exercised below (never called with NULL elsewhere).
 *   UNGUARDED -- raw-C / platform-delegated wrapper with NO NULL test; passing
 *                NULL is undefined behaviour in the C language / host CRT.
 *                These are NEVER called with NULL here; they are enumerated in
 *                the `unguarded[]` table and in the UNGUARDED comment block.
 *   VALID-NULL -- ISO C explicitly permits NULL in this position (mbstate_t,
 *                pwc/dst length queries, nan tagp, time out-param, locale
 *                query). These must be ACCEPTED (no invented EINVAL rejection).
 *
 * The corpus is table-driven: `guarded[]` holds (api, null-position, expected
 * normalized return, expected errno) descriptors. Each descriptor is executed
 * with NVARIANTS distinct "valid elsewhere" values, and every execution is run
 * twice -- once with logging disabled, once with logging enabled -- and the
 * normalized return + errno must match. A SIGSEGV/SIGBUS/SIGABRT handler prints
 * a `FINDING:` naming the exact guarded call if a guard is missing.
 *
 * Normalized return convention: pointer -> 0 (NULL) / 1 (non-NULL); integer ->
 * value; size_t/wint_t (size_t)-1/WEOF -> -1; floating 0.0 -> 0 else 1; void -> 0.
 * exp_ret == -2 means "g_vn" (the per-variant length); F_NORET skips ret checks.
 */
#include "cplus_libc.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

/* ------------------------------------------------------------------------- */
/* Harness state                                                             */
/* ------------------------------------------------------------------------- */
static int g_fail = 0;
static long long g_ret;
static int g_err;
static char g_cur[512];
static size_t g_vn;
static long long g_scenarios;

static void crash_handler(int sig) {
    fprintf(stdout, "FINDING: crash signal %d in guarded scenario: %s\n", sig, g_cur);
    fflush(stdout);
    _exit(70);
}

static void check(int ok, const char *what) {
    if (!ok) {
        fprintf(stdout, "FINDING: %s | scenario=%s | errno=%d\n", what, g_cur, g_err);
        fflush(stdout);
        g_fail++;
    }
}
#define CHK(c) check((c) ? 1 : 0, #c)

static void setret_p(const void *p) { g_ret = p ? 1 : 0; }
static void setret_i(long long v) { g_ret = v; }
static void setret_void(void) { g_ret = 0; }
static void setret_sz(size_t v) { g_ret = (v == (size_t)-1) ? -1 : (long long)v; }
static void setret_w(wint_t v) { g_ret = (v == (wint_t)-1) ? -1 : (long long)v; }
static void setret_d(double v) { g_ret = (v == 0.0) ? 0 : 1; }

#define CALLP(e)  do { void *_r = (void *)(e); g_err = errno; setret_p(_r); } while (0)
#define CALLI(e)  do { long long _r = (long long)(e); g_err = errno; setret_i(_r); } while (0)
#define CALLSZ(e) do { size_t _r = (size_t)(e); g_err = errno; setret_sz(_r); } while (0)
#define CALLW(e)  do { wint_t _r = (wint_t)(e); g_err = errno; setret_w(_r); } while (0)
#define CALLD(e)  do { double _r = (double)(e); g_err = errno; setret_d(_r); } while (0)
#define CALLV(e)  do { (void)(e); g_err = errno; setret_void(); } while (0)

/* Variadic calls are routed through plain function pointers so that the
 * format(printf/scanf,...) attributes on the public declarations do not cause
 * -Wformat diagnostics for NULL / non-literal format strings. */
static int (*const p_snprintf)(char *, size_t, const char *, ...) = cplus_snprintf;
static int (*const p_printf)(const char *, ...) = cplus_printf;
static int (*const p_fprintf)(FILE *, const char *, ...) = cplus_fprintf;
static int (*const p_sprintf)(char *, const char *, ...) = cplus_sprintf;
static int (*const p_scanf)(const char *, ...) = cplus_scanf;
static int (*const p_fscanf)(FILE *, const char *, ...) = cplus_fscanf;
static int (*const p_sscanf)(const char *, const char *, ...) = cplus_sscanf;
static int (*const p_wprintf)(const wchar_t *, ...) = cplus_wprintf;
static int (*const p_fwprintf)(FILE *, const wchar_t *, ...) = cplus_fwprintf;
static int (*const p_swprintf)(wchar_t *, size_t, const wchar_t *, ...) = cplus_swprintf;
static int (*const p_wscanf)(const wchar_t *, ...) = cplus_wscanf;
static int (*const p_fwscanf)(FILE *, const wchar_t *, ...) = cplus_fwscanf;
static int (*const p_swscanf)(const wchar_t *, const wchar_t *, ...) = cplus_swscanf;

static int (*const p_vsnprintf)(char *, size_t, const char *, va_list) = cplus_vsnprintf;
static int (*const p_vsprintf)(char *, const char *, va_list) = cplus_vsprintf;
static int (*const p_vfprintf)(FILE *, const char *, va_list) = cplus_vfprintf;
static int (*const p_vprintf)(const char *, va_list) = cplus_vprintf;
static int (*const p_vfscanf)(FILE *, const char *, va_list) = cplus_vfscanf;
static int (*const p_vsscanf)(const char *, const char *, va_list) = cplus_vsscanf;
static int (*const p_vwprintf)(const wchar_t *, va_list) = cplus_vwprintf;
static int (*const p_vfwprintf)(FILE *, const wchar_t *, va_list) = cplus_vfwprintf;
static int (*const p_vswprintf)(wchar_t *, size_t, const wchar_t *, va_list) = cplus_vswprintf;
static int (*const p_vwscanf)(const wchar_t *, va_list) = cplus_vwscanf;
static int (*const p_vfwscanf)(FILE *, const wchar_t *, va_list) = cplus_vfwscanf;
static int (*const p_vswscanf)(const wchar_t *, const wchar_t *, va_list) = cplus_vswscanf;

/* va_list wrappers for the v-forms. */
static int tv_vsnprintf(char *s, size_t n, const char *f, ...) {
    va_list a; int r; va_start(a, f); r = p_vsnprintf(s, n, f, a); va_end(a); return r;
}
static int tv_vsprintf(char *s, const char *f, ...) {
    va_list a; int r; va_start(a, f); r = p_vsprintf(s, f, a); va_end(a); return r;
}
static int tv_vfprintf(FILE *st, const char *f, ...) {
    va_list a; int r; va_start(a, f); r = p_vfprintf(st, f, a); va_end(a); return r;
}
static int tv_vprintf(const char *f, ...) {
    va_list a; int r; va_start(a, f); r = p_vprintf(f, a); va_end(a); return r;
}
static int tv_vfscanf(FILE *st, const char *f, ...) {
    va_list a; int r; va_start(a, f); r = p_vfscanf(st, f, a); va_end(a); return r;
}
static int tv_vsscanf(const char *s, const char *f, ...) {
    va_list a; int r; va_start(a, f); r = p_vsscanf(s, f, a); va_end(a); return r;
}
static int tv_vwprintf(const wchar_t *f, ...) {
    va_list a; int r; va_start(a, f); r = p_vwprintf(f, a); va_end(a); return r;
}
static int tv_vfwprintf(FILE *st, const wchar_t *f, ...) {
    va_list a; int r; va_start(a, f); r = p_vfwprintf(st, f, a); va_end(a); return r;
}
static int tv_vswprintf(wchar_t *s, size_t n, const wchar_t *f, ...) {
    va_list a; int r; va_start(a, f); r = p_vswprintf(s, n, f, a); va_end(a); return r;
}
static int tv_vwscanf(const wchar_t *f, ...) {
    va_list a; int r; va_start(a, f); r = p_vwscanf(f, a); va_end(a); return r;
}
static int tv_vfwscanf(FILE *st, const wchar_t *f, ...) {
    va_list a; int r; va_start(a, f); r = p_vfwscanf(st, f, a); va_end(a); return r;
}
static int tv_vswscanf(const wchar_t *s, const wchar_t *f, ...) {
    va_list a; int r; va_start(a, f); r = p_vswscanf(s, f, a); va_end(a); return r;
}

/* ------------------------------------------------------------------------- */
/* Descriptor table                                                          */
/* ------------------------------------------------------------------------- */
#define F_NORET  1u  /* do not compare normalized return */
#define F_ACCEPT 2u  /* VALID-NULL: must not be rejected with EINVAL */

typedef struct {
    const char *name;
    int op;        /* dispatch opcode */
    int np;        /* 0-based argument index that is NULL */
    long long exp_ret;
    int exp_errno; /* -1 = not documented / do not check */
    unsigned flags;
} case_desc;

/* Opcodes (one per guarded api). */
enum {
    OP_MEMCPY, OP_MEMMOVE, OP_MEMSET, OP_MEMCMP, OP_STRLEN, OP_STRCPY,
    OP_STRNCPY, OP_STRCAT, OP_STRNCAT, OP_STRCMP, OP_STRNCMP, OP_STRNLEN,
    OP_STRDUP, OP_STRNDUP, OP_STRSPN, OP_STRCSPN, OP_STRPBRK, OP_STRTOK_R,
    OP_STRSEP,
    OP_FREE, OP_FREE_CHECKED, OP_REALLOC, OP_ALLOC_GET_META,
    OP_ALLOC_QUERY_LENGTH, OP_ALLOC_CHECK_GENERATION, OP_TRANSFER_OWNERSHIP,
    OP_STRTOIMAX, OP_STRTOUMAX, OP_MBSTOWCS, OP_WCSTOMBS, OP_ATOI, OP_ATOL,
    OP_FOPEN, OP_FCLOSE, OP_FREAD, OP_FWRITE, OP_VSNPRINTF, OP_SNPRINTF,
    OP_PRINTF, OP_FPRINTF, OP_SCANF, OP_FSCANF, OP_SPRINTF, OP_VSPRINTF,
    OP_SSCANF, OP_VSSCANF, OP_VFPRINTF, OP_VPRINTF, OP_VFSCANF, OP_REWIND,
    OP_FGETC, OP_FPUTC,
    OP_WPRINTF, OP_VWPRINTF, OP_FWPRINTF, OP_VFWPRINTF, OP_SWPRINTF,
    OP_VSWPRINTF, OP_WSCANF, OP_VWSCANF, OP_FWSCANF, OP_VFWSCANF,
    OP_SWSCANF, OP_VSWSCANF,
    OP_MBSRTOWCS, OP_WCSRTOMBS, OP_FPUTWC, OP_FGETWC, OP_PUTWC, OP_GETWC,
    OP_UNGETWC,
    OP_WCSTOL, OP_WCSTOLL, OP_WCSTOUL, OP_WCSTOULL, OP_WCSTOD, OP_WCSTOF,
    OP_WCSTOLD, OP_WCSTOK, OP_WCSFMTIME,
    OP_WCSTOIMAX, OP_WCSTOUMAX,
    OP_ASCTIME, OP_CTIME, OP_GMTIME_R, OP_LOCALTIME_R, OP_ASCTIME_R,
    OP_CTIME_R, OP_FEGETEXCEPTFLAG, OP_FEHOLDEXCEPT,
    OP_FREXPF, OP_MODFF, OP_REMQUOF, OP_REMQUOL, OP_FREXPL, OP_MODFL,
    OP_WCTYPE, OP_WCTRANS,
    /* VALID-NULL acceptance probes */
    OP_ACC_MBRTOWC, OP_ACC_MBRTOWC_S, OP_ACC_WCRTOMB, OP_ACC_WCRTOMB_PS,
    OP_ACC_MBSRTOWCS_DST, OP_ACC_MBSRTOWCS_PS, OP_ACC_WCSRTOMBS_DST,
    OP_ACC_WCSRTOMBS_PS, OP_ACC_NAN, OP_ACC_TIME, OP_ACC_SETLOCALE
};

#define G(n, o, p, r, e)      { n, o, p, r, e, 0u }
#define GN(n, o, p, e)        { n, o, p, -2, e, 0u }   /* expect g_vn */
#define A(n, o, p)            { n, o, p, -1, -1, F_NORET | F_ACCEPT }

static const case_desc guarded[] = {
    /* --- string / memory (guarded) --------------------------------------- */
    G("memcpy[dest]",       OP_MEMCPY,     0, 0,  EINVAL),
    G("memcpy[src]",        OP_MEMCPY,     1, 1,  EINVAL),
    G("memmove[dest]",      OP_MEMMOVE,    0, 0,  EINVAL),
    G("memmove[src]",       OP_MEMMOVE,    1, 1,  EINVAL),
    G("memset[s]",          OP_MEMSET,     0, 0,  EINVAL),
    G("memcmp[s1]",         OP_MEMCMP,     0, -1, EINVAL),
    G("memcmp[s2]",         OP_MEMCMP,     1, 1,  EINVAL),
    G("strlen[s]",          OP_STRLEN,     0, 0,  0),
    G("strcpy[dest]",       OP_STRCPY,     0, 0,  EINVAL),
    G("strcpy[src]",        OP_STRCPY,     1, 1,  EINVAL),
    G("strncpy[dest]",      OP_STRNCPY,    0, 0,  EINVAL),
    G("strncpy[src]",       OP_STRNCPY,    1, 1,  EINVAL),
    G("strcat[dest]",       OP_STRCAT,     0, 0,  EINVAL),
    G("strcat[src]",        OP_STRCAT,     1, 1,  EINVAL),
    G("strncat[dest]",      OP_STRNCAT,    0, 0,  EINVAL),
    G("strncat[src]",       OP_STRNCAT,    1, 1,  EINVAL),
    G("strcmp[s1]",         OP_STRCMP,     0, -1, EINVAL),
    G("strcmp[s2]",         OP_STRCMP,     1, 1,  EINVAL),
    G("strncmp[s1]",        OP_STRNCMP,    0, -1, EINVAL),
    G("strncmp[s2]",        OP_STRNCMP,    1, 1,  EINVAL),
    G("strnlen[s]",         OP_STRNLEN,    0, 0,  0),
    G("strdup[s]",          OP_STRDUP,     0, 0,  0),
    G("strndup[s]",         OP_STRNDUP,    0, 0,  0),
    G("strspn[s]",          OP_STRSPN,     0, 0,  0),
    G("strspn[accept]",     OP_STRSPN,     1, 0,  0),
    G("strcspn[s]",         OP_STRCSPN,    0, 0,  0),
    GN("strcspn[reject]",   OP_STRCSPN,    1, 0),
    G("strpbrk[s]",         OP_STRPBRK,    0, 0,  0),
    G("strpbrk[accept]",    OP_STRPBRK,    1, 0,  0),
    G("strtok_r[s]",        OP_STRTOK_R,   0, 0,  0),
    G("strsep[sep]",        OP_STRSEP,     1, 1,  0),

    /* --- stdlib (guarded) ------------------------------------------------- */
    G("free[ptr]",              OP_FREE,                 0, 0, 0),
    G("free_checked[inout]",    OP_FREE_CHECKED,         0, EINVAL, EINVAL),
    G("realloc[ptr]",           OP_REALLOC,              0, 1, -1),
    G("alloc_get_meta[ptr]",    OP_ALLOC_GET_META,       0, EINVAL, EINVAL),
    G("alloc_get_meta[out]",    OP_ALLOC_GET_META,       1, EINVAL, EINVAL),
    G("alloc_query_length[ptr]",OP_ALLOC_QUERY_LENGTH,   0, EINVAL, EINVAL),
    G("alloc_query_length[out]",OP_ALLOC_QUERY_LENGTH,   2, EINVAL, EINVAL),
    G("alloc_check_generation[ptr]", OP_ALLOC_CHECK_GENERATION, 0, 0, 0),
    G("transfer_ownership[dst]",OP_TRANSFER_OWNERSHIP,   0, EINVAL, EINVAL),
    G("transfer_ownership[src]",OP_TRANSFER_OWNERSHIP,   1, EINVAL, EINVAL),
    G("strtoimax[nptr]",        OP_STRTOIMAX,            0, 0, EINVAL),
    G("strtoumax[nptr]",        OP_STRTOUMAX,            0, 0, EINVAL),
    G("mbstowcs[s]",            OP_MBSTOWCS,             1, -1, EINVAL),
    G("mbstowcs[pwcs]",         OP_MBSTOWCS,             0, -1, EINVAL),
    G("wcstombs[pwcs]",         OP_WCSTOMBS,             0, -1, EINVAL),
    G("wcstombs[s]",            OP_WCSTOMBS,             1, -1, EINVAL),
    G("atoi[s]",                OP_ATOI,                 0, 0, 0),
    G("atol[s]",                OP_ATOL,                 0, 0, 0),

    /* --- stdio (guarded) -------------------------------------------------- */
    G("fopen[path]",        OP_FOPEN,     0, 0,  EINVAL),
    G("fopen[mode]",        OP_FOPEN,     1, 0,  EINVAL),
    G("fclose[stream]",     OP_FCLOSE,    0, -1, EINVAL),
    G("fread[ptr]",         OP_FREAD,     0, 0,  EINVAL),
    G("fread[stream]",      OP_FREAD,     3, 0,  EINVAL),
    G("fwrite[ptr]",        OP_FWRITE,    0, 0,  EINVAL),
    G("fwrite[stream]",     OP_FWRITE,    3, 0,  EINVAL),
    G("vsnprintf[str]",     OP_VSNPRINTF, 0, -1, EINVAL),
    G("vsnprintf[fmt]",     OP_VSNPRINTF, 2, -1, EINVAL),
    G("snprintf[str]",      OP_SNPRINTF,  0, -1, EINVAL),
    G("snprintf[fmt]",      OP_SNPRINTF,  2, -1, EINVAL),
    G("printf[fmt]",        OP_PRINTF,    0, -1, EINVAL),
    G("fprintf[stream]",    OP_FPRINTF,   0, -1, EINVAL),
    G("fprintf[fmt]",       OP_FPRINTF,   1, -1, EINVAL),
    G("scanf[fmt]",         OP_SCANF,     0, -1, EINVAL),
    G("fscanf[stream]",     OP_FSCANF,    0, -1, EINVAL),
    G("fscanf[fmt]",        OP_FSCANF,    1, -1, EINVAL),
    G("sprintf[str]",       OP_SPRINTF,   0, -1, EINVAL),
    G("sprintf[fmt]",       OP_SPRINTF,   1, -1, EINVAL),
    G("vsprintf[str]",      OP_VSPRINTF,  0, -1, EINVAL),
    G("vsprintf[fmt]",      OP_VSPRINTF,  1, -1, EINVAL),
    G("sscanf[str]",        OP_SSCANF,    0, -1, EINVAL),
    G("sscanf[fmt]",        OP_SSCANF,    1, -1, EINVAL),
    G("vsscanf[str]",       OP_VSSCANF,   0, -1, EINVAL),
    G("vsscanf[fmt]",       OP_VSSCANF,   1, -1, EINVAL),
    G("vfprintf[stream]",   OP_VFPRINTF,  0, -1, EINVAL),
    G("vfprintf[fmt]",      OP_VFPRINTF,  1, -1, EINVAL),
    G("vprintf[fmt]",       OP_VPRINTF,   0, -1, EINVAL),
    G("vfscanf[stream]",    OP_VFSCANF,   0, -1, EINVAL),
    G("vfscanf[fmt]",       OP_VFSCANF,   1, -1, EINVAL),
    G("rewind[stream]",     OP_REWIND,    0, 0,  EINVAL),
    G("fgetc[stream]",      OP_FGETC,     0, -1, EINVAL),
    G("fputc[stream]",      OP_FPUTC,     1, -1, EINVAL),

    /* --- wide formatted I/O (guarded) ------------------------------------ */
    G("wprintf[fmt]",       OP_WPRINTF,   0, -1, EINVAL),
    G("vwprintf[fmt]",      OP_VWPRINTF,  0, -1, EINVAL),
    G("fwprintf[stream]",   OP_FWPRINTF,  0, -1, EINVAL),
    G("fwprintf[fmt]",      OP_FWPRINTF,  1, -1, EINVAL),
    G("vfwprintf[stream]",  OP_VFWPRINTF, 0, -1, EINVAL),
    G("vfwprintf[fmt]",     OP_VFWPRINTF, 1, -1, EINVAL),
    G("swprintf[s]",        OP_SWPRINTF,  0, -1, EINVAL),
    G("swprintf[fmt]",      OP_SWPRINTF,  2, -1, EINVAL),
    G("vswprintf[s]",       OP_VSWPRINTF, 0, -1, EINVAL),
    G("vswprintf[fmt]",     OP_VSWPRINTF, 2, -1, EINVAL),
    G("wscanf[fmt]",        OP_WSCANF,    0, -1, EINVAL),
    G("vwscanf[fmt]",       OP_VWSCANF,   0, -1, EINVAL),
    G("fwscanf[stream]",    OP_FWSCANF,   0, -1, EINVAL),
    G("fwscanf[fmt]",       OP_FWSCANF,   1, -1, EINVAL),
    G("vfwscanf[stream]",   OP_VFWSCANF,  0, -1, EINVAL),
    G("vfwscanf[fmt]",      OP_VFWSCANF,  1, -1, EINVAL),
    G("swscanf[str]",       OP_SWSCANF,   0, -1, EINVAL),
    G("swscanf[fmt]",       OP_SWSCANF,   1, -1, EINVAL),
    G("vswscanf[str]",      OP_VSWSCANF,  0, -1, EINVAL),
    G("vswscanf[fmt]",      OP_VSWSCANF,  1, -1, EINVAL),

    /* --- wide multibyte / stream (guarded) ------------------------------- */
    G("mbsrtowcs[src]",     OP_MBSRTOWCS, 1, -1, EINVAL),
    G("wcsrtombs[src]",     OP_WCSRTOMBS, 1, -1, EINVAL),
    G("fputwc[stream]",     OP_FPUTWC,    1, -1, EINVAL),
    G("fgetwc[stream]",     OP_FGETWC,    0, -1, EINVAL),
    G("putwc[stream]",      OP_PUTWC,     1, -1, EINVAL),
    G("getwc[stream]",      OP_GETWC,     0, -1, EINVAL),
    G("ungetwc[stream]",    OP_UNGETWC,   1, -1, EINVAL),

    /* --- wide numeric / time (guarded) ----------------------------------- */
    G("wcstol[nptr]",       OP_WCSTOL,    0, 0, EINVAL),
    G("wcstoll[nptr]",      OP_WCSTOLL,   0, 0, EINVAL),
    G("wcstoul[nptr]",      OP_WCSTOUL,   0, 0, EINVAL),
    G("wcstoull[nptr]",     OP_WCSTOULL,  0, 0, EINVAL),
    G("wcstod[nptr]",       OP_WCSTOD,    0, 0, EINVAL),
    G("wcstof[nptr]",       OP_WCSTOF,    0, 0, EINVAL),
    G("wcstold[nptr]",      OP_WCSTOLD,   0, 0, EINVAL),
    G("wcstok[delim]",      OP_WCSTOK,    1, 0, EINVAL),
    G("wcstok[saveptr]",    OP_WCSTOK,    2, 0, EINVAL),
    G("wcsftime[wcs]",      OP_WCSFMTIME, 0, 0, EINVAL),
    G("wcsftime[format]",   OP_WCSFMTIME, 2, 0, EINVAL),
    G("wcstoimax[nptr]",    OP_WCSTOIMAX, 0, 0, EINVAL),
    G("wcstoumax[nptr]",    OP_WCSTOUMAX, 0, 0, EINVAL),
    G("asctime[tm]",        OP_ASCTIME,   0, 0, EINVAL),
    G("ctime[timep]",       OP_CTIME,     0, 0, EINVAL),
    G("gmtime_r[timep]",    OP_GMTIME_R,  0, 0, EINVAL),
    G("gmtime_r[result]",   OP_GMTIME_R,  1, 0, EINVAL),
    G("localtime_r[timep]", OP_LOCALTIME_R, 0, 0, EINVAL),
    G("localtime_r[result]",OP_LOCALTIME_R, 1, 0, EINVAL),
    G("asctime_r[tm]",      OP_ASCTIME_R, 0, 0, EINVAL),
    G("asctime_r[buf]",     OP_ASCTIME_R, 1, 0, EINVAL),
    G("ctime_r[timep]",     OP_CTIME_R,   0, 0, EINVAL),
    G("ctime_r[buf]",       OP_CTIME_R,   1, 0, EINVAL),

    /* --- fenv / math / wctype (guarded) ---------------------------------- */
    G("fegetexceptflag[flagp]", OP_FEGETEXCEPTFLAG, 0, -1, EINVAL),
    G("feholdexcept[envp]",     OP_FEHOLDEXCEPT,    0, -1, EINVAL),
    G("frexpf[e]",          OP_FREXPF,    1, 0, EINVAL),
    G("modff[ip]",          OP_MODFF,     1, 0, EINVAL),
    G("remquof[quo]",       OP_REMQUOF,   2, 0, EINVAL),
    G("remquol[quo]",       OP_REMQUOL,   2, 0, EINVAL),
    G("frexpl[exp]",        OP_FREXPL,    1, 0, EINVAL),
    G("modfl[iptr]",        OP_MODFL,     1, 0, EINVAL),
    G("wctype[property]",   OP_WCTYPE,    0, 0, EINVAL),
    G("wctrans[property]",  OP_WCTRANS,   0, 0, EINVAL),

    /* --- VALID-NULL: must be accepted, not rejected ---------------------- */
    A("mbrtowc[pwc=NULL]",          OP_ACC_MBRTOWC,        0),
    A("mbrtowc[s=NULL]",            OP_ACC_MBRTOWC_S,      1),
    A("wcrtomb[s=NULL]",            OP_ACC_WCRTOMB,        0),
    A("wcrtomb[ps=NULL]",           OP_ACC_WCRTOMB_PS,     2),
    A("mbsrtowcs[dst=NULL]",        OP_ACC_MBSRTOWCS_DST,  0),
    A("mbsrtowcs[ps=NULL]",         OP_ACC_MBSRTOWCS_PS,   3),
    A("wcsrtombs[dst=NULL]",        OP_ACC_WCSRTOMBS_DST,  0),
    A("wcsrtombs[ps=NULL]",         OP_ACC_WCSRTOMBS_PS,   3),
    A("nan[tagp=NULL]",             OP_ACC_NAN,            0),
    A("time[t=NULL]",               OP_ACC_TIME,           0),
    A("setlocale[locale=NULL]",     OP_ACC_SETLOCALE,      1)
};

/* ------------------------------------------------------------------------- */
/* UNGUARDED (documented raw-C UB): the following cplus_* entry points take a
 * pointer parameter but contain NO NULL guard in libc/src -- they forward
 * straight to raw C / the host CRT, so NULL there is undefined behaviour and
 * is deliberately NOT exercised in this corpus.  Partially-optional NULLs are
 * noted.  This is an honest finding, not a pass.
 *
 *   string/memory:
 *     cplus_memchr cplus_memrchr cplus_strchr cplus_strrchr cplus_strstr
 *     cplus_strcasecmp cplus_strncasecmp cplus_memccpy cplus_stpcpy
 *     cplus_stpncpy cplus_memmem cplus_strverscmp cplus_strcasestr
 *     cplus_strchrnul cplus_strcoll cplus_strxfrm cplus_strlcpy cplus_strlcat
 *     cplus_strnstr cplus_swab cplus_strerror_r
 *     cplus_strtok  (s==NULL is a legal continuation; delim==NULL is UB)
 *     cplus_strtok_r[saveptr] and cplus_strsep[str] are UNGUARDED positions
 *       (the source dereferences *p / *str before/after its NULL checks);
 *       only the s / sep positions of those two APIs are guarded.
 *   stdlib:
 *     cplus_qsort cplus_bsearch cplus_qsort_r cplus_setenv cplus_unsetenv
 *     cplus_putenv cplus_getenv cplus_system cplus_mkstemp cplus_mkdtemp
 *     cplus_realpath cplus_ecvt cplus_fcvt cplus_gcvt cplus_erand48
 *     cplus_seed48 cplus_atoll cplus_rand_r cplus_atexit cplus_strtol
 *     cplus_strtoul cplus_strtoll cplus_strtoull cplus_strtod cplus_atof
 *     cplus_strtof cplus_strtold
 *   stdio:
 *     cplus_fseek cplus_ftell cplus_fflush cplus_remove cplus_rename
 *     cplus_fgets cplus_fputs cplus_getc cplus_putc cplus_feof cplus_ferror
 *     cplus_clearerr cplus_puts cplus_perror cplus_setvbuf cplus_setbuf
 *     cplus_fgetpos cplus_fsetpos cplus_ungetc cplus_fileno cplus_fdopen
 *     cplus_freopen
 *   wchar:
 *     cplus_wcslen cplus_wcscpy cplus_wcsncpy cplus_wcscmp cplus_wcsncmp
 *     cplus_wcscat cplus_wcsstr cplus_wmemcpy cplus_wmemmove cplus_wmemset
 *     cplus_wmemcmp
 *   math (double-family pointer out-params have no guard; float/long-double
 *     counterparts do):
 *     cplus_frexp cplus_modf cplus_remquo
 *   fenv:
 *     cplus_fegetenv cplus_fesetenv cplus_fesetexceptflag cplus_feupdateenv
 *   time:
 *     cplus_gmtime cplus_localtime cplus_mktime cplus_strftime
 *     cplus_clock_gettime cplus_nanosleep(req; rem may be NULL) cplus_timespec_get
 *   signal:
 *     cplus_sigemptyset cplus_sigfillset cplus_sigaddset cplus_sigdelset
 *     cplus_sigismember cplus_sigsuspend cplus_sigwait cplus_signal
 *     (cplus_sigaction / cplus_sigprocmask have POSIX-optional NULL slots but
 *      add no explicit guard)
 *   threads:
 *     cplus_pthread_create(thread/start; attr may be NULL)
 *     cplus_pthread_mutex_init/lock/unlock/destroy
 *     cplus_pthread_cond_init/signal/broadcast/wait/destroy
 *
 * VALID-NULL by ISO C (no guard needed, accepted above): mbstate_t* ps in the
 * restartable multibyte family, pwc/dst length-query slots, mbrtowc/wcrtomb
 * reset strings, nan() tagp, time() out-param, setlocale() query locale.
 * ------------------------------------------------------------------------- */
static const char *const unguarded[] = {
    "cplus_memchr","cplus_memrchr","cplus_strchr","cplus_strrchr","cplus_strstr",
    "cplus_strcasecmp","cplus_strncasecmp","cplus_memccpy","cplus_stpcpy",
    "cplus_stpncpy","cplus_memmem","cplus_strverscmp","cplus_strcasestr",
    "cplus_strchrnul","cplus_strcoll","cplus_strxfrm","cplus_strlcpy",
    "cplus_strlcat","cplus_strnstr","cplus_swab","cplus_strerror_r","cplus_strtok",
    "cplus_strtok_r[saveptr]","cplus_strsep[str]",
    "cplus_qsort","cplus_bsearch","cplus_qsort_r","cplus_setenv","cplus_unsetenv",
    "cplus_putenv","cplus_getenv","cplus_system","cplus_mkstemp","cplus_mkdtemp",
    "cplus_realpath","cplus_ecvt","cplus_fcvt","cplus_gcvt","cplus_erand48",
    "cplus_seed48","cplus_atoll","cplus_rand_r","cplus_atexit","cplus_strtol",
    "cplus_strtoul","cplus_strtoll","cplus_strtoull","cplus_strtod","cplus_atof",
    "cplus_strtof","cplus_strtold",
    "cplus_fseek","cplus_ftell","cplus_fflush","cplus_remove","cplus_rename",
    "cplus_fgets","cplus_fputs","cplus_getc","cplus_putc","cplus_feof",
    "cplus_ferror","cplus_clearerr","cplus_puts","cplus_perror","cplus_setvbuf",
    "cplus_setbuf","cplus_fgetpos","cplus_fsetpos","cplus_ungetc","cplus_fileno",
    "cplus_fdopen","cplus_freopen",
    "cplus_wcslen","cplus_wcscpy","cplus_wcsncpy","cplus_wcscmp","cplus_wcsncmp",
    "cplus_wcscat","cplus_wcsstr","cplus_wmemcpy","cplus_wmemmove","cplus_wmemset",
    "cplus_wmemcmp",
    "cplus_frexp","cplus_modf","cplus_remquo",
    "cplus_fegetenv","cplus_fesetenv","cplus_fesetexceptflag","cplus_feupdateenv",
    "cplus_gmtime","cplus_localtime","cplus_mktime","cplus_strftime",
    "cplus_clock_gettime","cplus_nanosleep","cplus_timespec_get",
    "cplus_sigemptyset","cplus_sigfillset","cplus_sigaddset","cplus_sigdelset",
    "cplus_sigismember","cplus_sigsuspend","cplus_sigwait","cplus_signal",
    "cplus_pthread_create","cplus_pthread_mutex_init","cplus_pthread_mutex_lock",
    "cplus_pthread_mutex_unlock","cplus_pthread_mutex_destroy",
    "cplus_pthread_cond_init","cplus_pthread_cond_signal",
    "cplus_pthread_cond_broadcast","cplus_pthread_cond_wait",
    "cplus_pthread_cond_destroy"
};

/* ------------------------------------------------------------------------- */
/* Scenario buffers / variant preparation                                    */
/* ------------------------------------------------------------------------- */
#define NVARIANTS 4
static const size_t vn_tab[NVARIANTS] = { 1u, 3u, 8u, 31u };
static char ba[128], bb[128];
static wchar_t wa[64], wb[64];
static char a_buf[64];
static char *a_ptr;
static cplus_alloc_meta meta;
static size_t out_count;
static unsigned long long gen = 1ULL;
static void *dst_ptr, *src_ptr;
static char *ep;
static wchar_t *wep;
static wchar_t *saveptr;
static char *csaveptr;
static FILE *nullstream;
static struct tm tm_val;
static time_t tv;
static fexcept_t excflag;
static fenv_t fenv_val;
static int quo;
static int expn;
static mbstate_t mbst;

static void prep(int v) {
    size_t n = vn_tab[v % NVARIANTS];
    g_vn = n;
    memset(ba, 'a', sizeof ba);
    memset(bb, 'b', sizeof bb);
    ba[n % (sizeof ba - 1)] = '\0';
    bb[n % (sizeof bb - 1)] = '\0';
    ba[0] = 'a';
    bb[0] = 'b';
    wa[0] = L'a'; wa[1] = L'b'; wa[2] = L'\0';
    wb[0] = L'x'; wb[1] = L'y'; wb[2] = L'\0';
    memset(a_buf, 'z', sizeof a_buf);
    a_ptr = a_buf;
    src_ptr = (void *)ba;
    dst_ptr = NULL;
    ep = NULL;
    wep = NULL;
    saveptr = NULL;
    csaveptr = NULL;
    nullstream = NULL;
    memset(&tm_val, 0, sizeof tm_val);
    tm_val.tm_year = 100;
    tm_val.tm_mon = 0;
    tm_val.tm_mday = 1;
    tv = (time_t)0;
    memset(&mbst, 0, sizeof mbst);
    memset(&meta, 0, sizeof meta);
    out_count = 0;
    gen = 1ULL;
    quo = 0;
    expn = 0;
}

/* Perform exactly one guarded/valid-NULL call described by c. */
static void run_call(const case_desc *c) {
    int np = c->np;
    switch (c->op) {
    /* --- string / memory --- */
    case OP_MEMCPY:   CALLP(cplus_memcpy(np==0?NULL:(void*)ba, np==1?NULL:(const void*)bb, g_vn)); break;
    case OP_MEMMOVE:  CALLP(cplus_memmove(np==0?NULL:(void*)ba, np==1?NULL:(const void*)bb, g_vn)); break;
    case OP_MEMSET:   CALLP(cplus_memset(np==0?NULL:(void*)ba, 'x', g_vn)); break;
    case OP_MEMCMP:   CALLI(cplus_memcmp(np==0?NULL:(const void*)ba, np==1?NULL:(const void*)bb, g_vn)); break;
    case OP_STRLEN:   CALLSZ(cplus_strlen(np==0?NULL:ba)); break;
    case OP_STRCPY:   CALLP(cplus_strcpy(np==0?NULL:ba, np==1?NULL:bb)); break;
    case OP_STRNCPY:  CALLP(cplus_strncpy(np==0?NULL:ba, np==1?NULL:bb, g_vn)); break;
    case OP_STRCAT:   CALLP(cplus_strcat(np==0?NULL:ba, np==1?NULL:bb)); break;
    case OP_STRNCAT:  CALLP(cplus_strncat(np==0?NULL:ba, np==1?NULL:bb, g_vn)); break;
    case OP_STRCMP:   CALLI(cplus_strcmp(np==0?NULL:ba, np==1?NULL:bb)); break;
    case OP_STRNCMP:  CALLI(cplus_strncmp(np==0?NULL:ba, np==1?NULL:bb, g_vn)); break;
    case OP_STRNLEN:  CALLSZ(cplus_strnlen(np==0?NULL:ba, g_vn)); break;
    case OP_STRDUP:   { char *r = cplus_strdup(np==0?NULL:ba); g_err=errno; if(r) cplus_free(r); setret_p(r); break; }
    case OP_STRNDUP:  { char *r = cplus_strndup(np==0?NULL:ba, g_vn); g_err=errno; if(r) cplus_free(r); setret_p(r); break; }
    case OP_STRSPN:   CALLSZ(cplus_strspn(np==0?NULL:ba, np==1?NULL:bb)); break;
    case OP_STRCSPN:  CALLSZ(cplus_strcspn(np==0?NULL:ba, np==1?NULL:bb)); break;
    case OP_STRPBRK:  CALLP(cplus_strpbrk(np==0?NULL:ba, np==1?NULL:bb)); break;
    case OP_STRTOK_R: CALLP(cplus_strtok_r(np==0?NULL:a_ptr, ",", &csaveptr)); break;
    case OP_STRSEP:   CALLP(cplus_strsep(&a_ptr, np==1?NULL:"\t")); break;

    /* --- stdlib --- */
    case OP_FREE:              CALLV(cplus_free(np==0?NULL:(void*)ba)); break;
    case OP_FREE_CHECKED:      { void *p = np==0?NULL:(void*)NULL; CALLI(cplus_free_checked(np==0?NULL:&p)); break; }
    case OP_REALLOC:           { void *r = cplus_realloc(NULL, g_vn); g_err=errno; if(r) cplus_free(r); setret_p(r); break; }
    case OP_ALLOC_GET_META:    CALLI(cplus_alloc_get_meta(np==0?NULL:(const void*)ba, np==1?NULL:&meta)); break;
    case OP_ALLOC_QUERY_LENGTH:CALLI(cplus_alloc_query_length(np==0?NULL:(const void*)ba, 1u, np==2?NULL:&out_count)); break;
    case OP_ALLOC_CHECK_GENERATION: CALLI(cplus_alloc_check_generation(np==0?NULL:(const void*)ba, gen)); break;
    case OP_TRANSFER_OWNERSHIP:CALLI(cplus_transfer_ownership(np==0?NULL:&dst_ptr, np==1?NULL:&src_ptr)); break;
    case OP_STRTOIMAX:         { char *e=NULL; CALLI(cplus_strtoimax(np==0?NULL:"123", &e, 10)); break; }
    case OP_STRTOUMAX:         { char *e=NULL; CALLI(cplus_strtoumax(np==0?NULL:"123", &e, 10)); break; }
    case OP_MBSTOWCS:          { wchar_t w[32]; CALLSZ(cplus_mbstowcs(np==0?NULL:w, np==1?NULL:"abc", g_vn)); break; }
    case OP_WCSTOMBS:          { char s[32]; CALLSZ(cplus_wcstombs(np==0?NULL:s, np==1?NULL:L"abc", g_vn)); break; }
    case OP_ATOI:              CALLI(cplus_atoi(np==0?NULL:"123")); break;
    case OP_ATOL:              CALLI(cplus_atol(np==0?NULL:"123")); break;

    /* --- stdio --- */
    case OP_FOPEN:    CALLP(cplus_fopen(np==0?NULL:"/dev/null", np==1?NULL:"r")); break;
    case OP_FCLOSE:   CALLI(cplus_fclose(np==0?NULL:nullstream)); break;
    case OP_FREAD:    CALLSZ(cplus_fread(np==0?NULL:ba, 1u, g_vn, np==3?NULL:(FILE*)stdin)); break;
    case OP_FWRITE:   CALLSZ(cplus_fwrite(np==0?NULL:(const void*)ba, 1u, g_vn, np==3?NULL:(FILE*)stdout)); break;
    case OP_VSNPRINTF:CALLI(tv_vsnprintf(np==0?NULL:ba, g_vn, np==2?NULL:"%d", 7)); break;
    case OP_SNPRINTF: CALLI(p_snprintf(np==0?NULL:ba, g_vn, np==2?NULL:"%d", 7)); break;
    case OP_PRINTF:   CALLI(p_printf(np==0?NULL:"%d", 7)); break;
    case OP_FPRINTF:  CALLI(p_fprintf(np==0?NULL:(FILE*)stdout, np==1?NULL:"%d", 7)); break;
    case OP_SCANF:    CALLI(p_scanf(np==0?NULL:"%d")); break;
    case OP_FSCANF:   CALLI(p_fscanf(np==0?NULL:(FILE*)stdin, np==1?NULL:"%d")); break;
    case OP_SPRINTF:  CALLI(p_sprintf(np==0?NULL:ba, np==1?NULL:"%d", 7)); break;
    case OP_VSPRINTF: CALLI(tv_vsprintf(np==0?NULL:ba, np==1?NULL:"%d", 7)); break;
    case OP_SSCANF:   CALLI(p_sscanf(np==0?NULL:"7", np==1?NULL:"%d")); break;
    case OP_VSSCANF:  CALLI(tv_vsscanf(np==0?NULL:"7", np==1?NULL:"%d")); break;
    case OP_VFPRINTF: CALLI(tv_vfprintf(np==0?NULL:(FILE*)stdout, np==1?NULL:"%d", 7)); break;
    case OP_VPRINTF:  CALLI(tv_vprintf(np==0?NULL:"%d", 7)); break;
    case OP_VFSCANF:  CALLI(tv_vfscanf(np==0?NULL:(FILE*)stdin, np==1?NULL:"%d")); break;
    case OP_REWIND:   CALLV(cplus_rewind(np==0?NULL:(FILE*)stdin)); break;
    case OP_FGETC:    CALLI(cplus_fgetc(np==0?NULL:(FILE*)stdin)); break;
    case OP_FPUTC:    CALLI(cplus_fputc('x', np==1?NULL:(FILE*)stdout)); break;

    /* --- wide formatted I/O --- */
    case OP_WPRINTF:  CALLI(p_wprintf(np==0?NULL:L"%d", 7)); break;
    case OP_VWPRINTF: CALLI(tv_vwprintf(np==0?NULL:L"%d", 7)); break;
    case OP_FWPRINTF: CALLI(p_fwprintf(np==0?NULL:(FILE*)stdout, np==1?NULL:L"%d", 7)); break;
    case OP_VFWPRINTF:CALLI(tv_vfwprintf(np==0?NULL:(FILE*)stdout, np==1?NULL:L"%d", 7)); break;
    case OP_SWPRINTF: CALLI(p_swprintf(np==0?NULL:wa, (size_t)16, np==2?NULL:L"%d", 7)); break;
    case OP_VSWPRINTF:CALLI(tv_vswprintf(np==0?NULL:wa, (size_t)16, np==2?NULL:L"%d", 7)); break;
    case OP_WSCANF:   CALLI(p_wscanf(np==0?NULL:L"%d")); break;
    case OP_VWSCANF:  CALLI(tv_vwscanf(np==0?NULL:L"%d")); break;
    case OP_FWSCANF:  CALLI(p_fwscanf(np==0?NULL:(FILE*)stdin, np==1?NULL:L"%d")); break;
    case OP_VFWSCANF: CALLI(tv_vfwscanf(np==0?NULL:(FILE*)stdin, np==1?NULL:L"%d")); break;
    case OP_SWSCANF:  CALLI(p_swscanf(np==0?NULL:L"7", np==1?NULL:L"%d")); break;
    case OP_VSWSCANF: CALLI(tv_vswscanf(np==0?NULL:L"7", np==1?NULL:L"%d")); break;

    /* --- wide multibyte / stream --- */
    case OP_MBSRTOWCS: { const char *s="abc"; const char **sp=&s; CALLSZ(cplus_mbsrtowcs(wa, np==1?NULL:sp, 8, &mbst)); break; }
    case OP_WCSRTOMBS: { const wchar_t *s=L"abc"; const wchar_t **sp=&s; CALLSZ(cplus_wcsrtombs(ba, np==1?NULL:sp, 8, &mbst)); break; }
    case OP_FPUTWC:    CALLW(cplus_fputwc(L'x', np==1?NULL:(FILE*)stdout)); break;
    case OP_FGETWC:    CALLW(cplus_fgetwc(np==0?NULL:(FILE*)stdin)); break;
    case OP_PUTWC:     CALLW(cplus_putwc(L'x', np==1?NULL:(FILE*)stdout)); break;
    case OP_GETWC:     CALLW(cplus_getwc(np==0?NULL:(FILE*)stdin)); break;
    case OP_UNGETWC:   CALLW(cplus_ungetwc(L'x', np==1?NULL:(FILE*)stdin)); break;

    /* --- wide numeric / time --- */
    case OP_WCSTOL:   CALLI(cplus_wcstol(np==0?NULL:L"123", &wep, 10)); break;
    case OP_WCSTOLL:  CALLI(cplus_wcstoll(np==0?NULL:L"123", &wep, 10)); break;
    case OP_WCSTOUL:  CALLI(cplus_wcstoul(np==0?NULL:L"123", &wep, 10)); break;
    case OP_WCSTOULL: CALLI(cplus_wcstoull(np==0?NULL:L"123", &wep, 10)); break;
    case OP_WCSTOD:   CALLD(cplus_wcstod(np==0?NULL:L"1.5", &wep)); break;
    case OP_WCSTOF:   CALLD(cplus_wcstof(np==0?NULL:L"1.5", &wep)); break;
    case OP_WCSTOLD:  CALLD(cplus_wcstold(np==0?NULL:L"1.5", &wep)); break;
    case OP_WCSTOK:   { wchar_t s[8]; wcscpy(s,L"a b"); CALLP(cplus_wcstok(s, np==1?NULL:L" ", np==2?NULL:&saveptr)); break; }
    case OP_WCSFMTIME:CALLSZ(cplus_wcsftime(np==0?NULL:wa, 16, np==2?NULL:L"%Y", &tm_val)); break;
    case OP_WCSTOIMAX:CALLI(cplus_wcstoimax(np==0?NULL:L"123", &wep, 10)); break;
    case OP_WCSTOUMAX:CALLI(cplus_wcstoumax(np==0?NULL:L"123", &wep, 10)); break;
    case OP_ASCTIME:  CALLP(cplus_asctime(np==0?NULL:&tm_val)); break;
    case OP_CTIME:    CALLP(cplus_ctime(np==0?NULL:&tv)); break;
    case OP_GMTIME_R: CALLP(cplus_gmtime_r(np==0?NULL:&tv, np==1?NULL:&tm_val)); break;
    case OP_LOCALTIME_R: CALLP(cplus_localtime_r(np==0?NULL:&tv, np==1?NULL:&tm_val)); break;
    case OP_ASCTIME_R:CALLP(cplus_asctime_r(np==0?NULL:&tm_val, np==1?NULL:a_buf)); break;
    case OP_CTIME_R:  CALLP(cplus_ctime_r(np==0?NULL:&tv, np==1?NULL:a_buf)); break;

    /* --- fenv / math / wctype --- */
    case OP_FEGETEXCEPTFLAG: CALLI(cplus_fegetexceptflag(np==0?NULL:&excflag, FE_ALL_EXCEPT)); break;
    case OP_FEHOLDEXCEPT:    CALLI(cplus_feholdexcept(np==0?NULL:&fenv_val)); break;
    case OP_FREXPF:   CALLD(cplus_frexpf(1.5f, np==1?NULL:&expn)); break;
    case OP_MODFF:    { float ip=0.0f; CALLD(cplus_modff(1.5f, np==1?NULL:&ip)); break; }
    case OP_REMQUOF:  CALLD(cplus_remquof(1.5f, 1.0f, np==2?NULL:&quo)); break;
    case OP_REMQUOL:  CALLD(cplus_remquol(1.5L, 1.0L, np==2?NULL:&quo)); break;
    case OP_FREXPL:   CALLD(cplus_frexpl(1.5L, np==1?NULL:&expn)); break;
    case OP_MODFL:    { long double ip=0.0L; CALLD(cplus_modfl(1.5L, np==1?NULL:&ip)); break; }
    case OP_WCTYPE:   { wctype_t w = cplus_wctype(np==0?NULL:"alpha"); g_err=errno; setret_i((long long)(w==0?0:1)); break; }
    case OP_WCTRANS:  { wctrans_t w = cplus_wctrans(np==0?NULL:"toupper"); g_err=errno; setret_i((long long)(w==0?0:1)); break; }

    /* --- VALID-NULL acceptance probes --- */
    case OP_ACC_MBRTOWC:      CALLW(cplus_mbrtowc(NULL, "a", 1, NULL)); break;
    case OP_ACC_MBRTOWC_S:    { wchar_t wc=0; CALLW(cplus_mbrtowc(&wc, NULL, 1, NULL)); break; }
    case OP_ACC_WCRTOMB:      CALLSZ(cplus_wcrtomb(NULL, L'a', NULL)); break;
    case OP_ACC_WCRTOMB_PS:   CALLSZ(cplus_wcrtomb(a_buf, L'a', NULL)); break;
    case OP_ACC_MBSRTOWCS_DST:{ const char*s="abc";const char**sp=&s; CALLSZ(cplus_mbsrtowcs(NULL, sp, 8, &mbst)); break; }
    case OP_ACC_MBSRTOWCS_PS: { const char*s="abc";const char**sp=&s; CALLSZ(cplus_mbsrtowcs(NULL, sp, 8, NULL)); break; }
    case OP_ACC_WCSRTOMBS_DST:{ const wchar_t*s=L"abc";const wchar_t**sp=&s; CALLSZ(cplus_wcsrtombs(NULL, sp, 8, &mbst)); break; }
    case OP_ACC_WCSRTOMBS_PS: { const wchar_t*s=L"abc";const wchar_t**sp=&s; CALLSZ(cplus_wcsrtombs(NULL, sp, 8, NULL)); break; }
    case OP_ACC_NAN:          CALLD(cplus_nan(NULL)); break;
    case OP_ACC_TIME:         CALLI((long long)cplus_time(NULL)); break;
    case OP_ACC_SETLOCALE:    { char *r=cplus_setlocale(LC_ALL, NULL); g_err=errno; setret_p(r); break; }
    default: check(0, "unknown opcode"); break;
    }
}

int main(void) {
    struct sigaction sa;
    size_t i, v;
    size_t ncase = sizeof(guarded) / sizeof(guarded[0]);
    (void)unguarded[0]; /* keep the UB-surface table emitted (it documents findings) */

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = crash_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);

    /* Keep the (intentionally noisy) opt-in logging off the terminal while
     * still exercising both logging states; FINDINGs go to stdout. */
    {
        int devnull = open("/dev/null", O_WRONLY);
        int saved = dup(STDERR_FILENO);
        if (devnull >= 0 && saved >= 0) dup2(devnull, STDERR_FILENO);
        if (devnull >= 0) close(devnull);

        cplus_libc_log_set_enabled(0);
        for (i = 0; i < ncase; ++i) {
            for (v = 0; v < NVARIANTS; ++v) {
                long long r0, r1;
                int e0, e1;
                const case_desc *c = &guarded[i];
                long long exp = (c->exp_ret == -2) ? (long long)vn_tab[v % NVARIANTS] : c->exp_ret;

                prep((int)v);
                snprintf(g_cur, sizeof g_cur, "%s [np=%d,var=%d]", c->name, c->np, (int)v);

                errno = 0;
                cplus_libc_log_set_enabled(0);
                run_call(c);
                r0 = g_ret; e0 = g_err;

                prep((int)v);
                errno = 0;
                cplus_libc_log_set_enabled(1);
                run_call(c);
                r1 = g_ret; e1 = g_err;

                cplus_libc_log_set_enabled(0);
                if (!(c->flags & F_NORET)) CHK(r0 == exp);
                if (c->exp_errno >= 0) CHK(e0 == c->exp_errno);
                if (c->flags & F_ACCEPT) CHK(e0 != EINVAL);
                /* logging must be observationally inert */
                CHK(r0 == r1);
                CHK(e0 == e1);
                g_scenarios++;
            }
        }
        if (saved >= 0) { dup2(saved, STDERR_FILENO); close(saved); }
    }

    printf("corpus_null: %llu guarded cases, %u unguarded documented\n",
           (unsigned long long)g_scenarios,
           (unsigned)(sizeof(unguarded) / sizeof(unguarded[0])));
    printf("corpus_null: %llu scenario executions, %d FINDING(s)\n",
           (unsigned long long)g_scenarios, g_fail);
    return g_fail ? 1 : 0;
}
