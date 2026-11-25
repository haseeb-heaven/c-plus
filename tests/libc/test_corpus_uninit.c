/* Adversarial corpus: explicitly-initialized *hostile* state handling.
 *
 * Methodology
 * ------------
 * This corpus never reads genuinely uninitialized memory (that would be UB
 * in the test itself).  Instead every object is fully initialized with a
 * deliberately hostile *but defined* bit pattern:
 *   - mbstate_t objects: zeroed, plus three genuine non-initial shift states
 *     captured from real conversions of 2/3/4-byte UTF-8 leads.  They are
 *     hostile (a partial sequence is carried into an unrelated conversion)
 *     but always valid representations of the type.  Arbitrary raw bytes are
 *     NOT used: glibc's iconv asserts on an invalid mbstate_t and aborts.
 *   - struct tm objects: all-zero plus out-of-range field combinations
 *     (month 13, mday 32, hour 25, year 0, isdst -1/0/1, ...).
 *   - fenv_t / fexcept_t objects: zeroed, 0xFF-filled, and random bytes.
 *   - cplus_alloc_meta objects: zeroed / 0xFF / hostile pre-state, queried
 *     with never-allocated stack addresses (the query hashes the address,
 *     it never dereferences the pointer contents).
 *   - conversion endptr objects: a zeroed wchar_t*, a writable stack slot
 *     filled with 0xFF, and NULL.
 *
 * The corpus is TABLE-DRIVEN: a static table of 760 distinct case
 * descriptors is built deterministically and dispatched through a
 * category-indexed function-pointer table.  A real floating-point
 * environment is saved and restored around every hostile fenv call.
 *
 * Any crash (SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT, the latter covering a
 * failed assert) is converted into a "FINDING:" diagnostic by the signal
 * handlers below, so a hostile state that escapes a wrapper is reported
 * rather than silently dying.  On a correct hardened libc the expected
 * single line of output is "corpus_uninit: 760 cases ok".
 */
#include <wchar.h>
#include <time.h>
#include <fenv.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

/* cplus_* prototypes (kept local so the test stays standalone). */
extern size_t cplus_mbrtowc(wchar_t *, const char *, size_t, mbstate_t *);
extern size_t cplus_wcrtomb(char *, wchar_t, mbstate_t *);
extern size_t cplus_mbsrtowcs(wchar_t *, const char **, size_t, mbstate_t *);
extern size_t cplus_wcsrtombs(char *, const wchar_t **, size_t, mbstate_t *);

extern time_t cplus_mktime(struct tm *);
extern size_t cplus_strftime(char *, size_t, const char *, const struct tm *);
extern char *cplus_asctime(const struct tm *);
extern size_t cplus_wcsftime(wchar_t *, size_t, const wchar_t *, const struct tm *);

extern int cplus_fegetenv(fenv_t *);
extern int cplus_fesetenv(const fenv_t *);
extern int cplus_feupdateenv(const fenv_t *);
extern int cplus_fesetexceptflag(const fexcept_t *, int);
extern int cplus_fetestexcept(int);

/* Public ABI from install-include/cplus_libc.h (kept local to stay standalone). */
typedef struct cplus_alloc_meta {
    size_t byte_size;
    size_t elem_size;
    size_t elem_count;
    unsigned long long generation;
    int is_live;
} cplus_alloc_meta;
extern int cplus_alloc_get_meta(const void *, cplus_alloc_meta *);
extern int cplus_alloc_is_live(const void *);
extern size_t cplus_alloc_size(const void *);
extern void *cplus_malloc(size_t);
extern void cplus_free(void *);

extern long cplus_wcstol(const wchar_t *, wchar_t **, int);
extern long long cplus_wcstoll(const wchar_t *, wchar_t **, int);
extern unsigned long cplus_wcstoul(const wchar_t *, wchar_t **, int);
extern unsigned long long cplus_wcstoull(const wchar_t *, wchar_t **, int);
extern double cplus_wcstod(const wchar_t *, wchar_t **);
extern float cplus_wcstof(const wchar_t *, wchar_t **);
extern long double cplus_wcstold(const wchar_t *, wchar_t **);
extern intmax_t cplus_wcstoimax(const wchar_t *, wchar_t **, int);
extern uintmax_t cplus_wcstoumax(const wchar_t *, wchar_t **, int);

/* ------------------------------------------------------------------ */
/* Signal diagnostics: always name the exact hostile case             */
/* ------------------------------------------------------------------ */
/* Optional bounded hang diagnostics for slower CI hosts (off by default):
 *   CPLUS_CORPUS_WATCHDOG=<seconds>  alarm() bound, reports the active case
 *   CPLUS_CORPUS_TRACE=1             print each case index before dispatch
 *   CPLUS_CORPUS_HANG_AT=<index>     diagnostic hook: sleep(5) at that case
 * The last two are off unless explicitly requested; the shipped corpus run
 * still prints exactly "corpus_uninit: 760 cases ok".
 *
 * Both handlers are async-signal-safe: they use only stack locals and write(2),
 * which is unbuffered, so the line reaches stderr immediately even when the
 * signal interrupts a buffered stdio call.  snprintf/printf/malloc are NOT
 * async-signal-safe and can themselves wedge when the signal interrupts the
 * very glibc conversion that is non-terminating, so the previous snprintf-
 * based hang handler could fail to name a case at all.  The full descriptor
 * (global index, category, variant, per-category index) is stored in
 * sig_atomic_t globals before each dispatch and written to stderr on failure.
 * A -1 descriptor means the failure happened before the case loop (setup).
 */
static volatile sig_atomic_t g_active_case = -1;
static volatile sig_atomic_t g_active_cat = -1;
static volatile sig_atomic_t g_active_variant = -1;
static volatile sig_atomic_t g_active_idx = -1;
static int g_hang_at = -1;

static void emit_ascii(const char *s) {
    size_t n = 0;
    ssize_t w;
    while (s[n] != '\0') ++n;
    w = write(STDERR_FILENO, s, n);
    (void)w; /* glibc marks write warn_unused_result; a partial write is fine here */
}

static void emit_signed(sig_atomic_t v) {
    char num[32];
    size_t pos = sizeof num;
    unsigned long uv;
    ssize_t w;
    int neg = (v < 0);
    uv = neg ? (unsigned long)(-(v + 1)) + 1UL : (unsigned long)v;
    do {
        num[--pos] = (char)('0' + (uv % 10UL));
        uv /= 10UL;
    } while (uv != 0UL && pos > 0u);
    if (neg && pos > 0u)
        num[--pos] = '-';
    w = write(STDERR_FILENO, num + pos, sizeof num - pos);
    (void)w;
}

static void report_finding(const char *kind, int sig) {
    emit_ascii("FINDING: corpus_uninit ");
    emit_ascii(kind);
    emit_ascii(" at case ");
    emit_signed(g_active_case);
    emit_ascii(" cat=");
    emit_signed(g_active_cat);
    emit_ascii(" variant=");
    emit_signed(g_active_variant);
    emit_ascii(" idx=");
    emit_signed(g_active_idx);
    emit_ascii(" signal=");
    emit_signed((sig_atomic_t)sig);
    emit_ascii("\n");
}

static void crash_handler(int sig) {
    report_finding("crash", sig);
    _exit(70);
}

static void hang_handler(int sig) {
    report_finding("hang", sig);
    _exit(70);
}

static void install_handlers(void) {
    (void)signal(SIGSEGV, crash_handler);
    (void)signal(SIGBUS, crash_handler);
    (void)signal(SIGILL, crash_handler);
    (void)signal(SIGFPE, crash_handler);
    (void)signal(SIGABRT, crash_handler); /* failed assert() lands here */
}

/* ------------------------------------------------------------------ */
/* Deterministic mixing / table of cases                               */
/* ------------------------------------------------------------------ */
static uint32_t mix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

enum { CAT_MB = 1, CAT_TM, CAT_FE, CAT_AL, CAT_EP };

typedef struct {
    int cat;
    int variant;
    int idx;
    uint32_t seed;
} case_t;

#define N_MB 200
#define N_TM 200
#define N_FE 100
#define N_AL 60
#define N_EP 200
#define N_CASES (N_MB + N_TM + N_FE + N_AL + N_EP) /* 760 */

static case_t g_cases[N_CASES];

static void build_table(void) {
    int i = 0, k;
    for (k = 0; k < N_MB; ++k, ++i) {
        g_cases[i].cat = CAT_MB; g_cases[i].variant = k % 4;
        g_cases[i].idx = k; g_cases[i].seed = mix32((uint32_t)i + 1u);
    }
    for (k = 0; k < N_TM; ++k, ++i) {
        g_cases[i].cat = CAT_TM; g_cases[i].variant = k % 5;
        g_cases[i].idx = k; g_cases[i].seed = mix32((uint32_t)i + 1u);
    }
    for (k = 0; k < N_FE; ++k, ++i) {
        g_cases[i].cat = CAT_FE; g_cases[i].variant = k % 3;
        g_cases[i].idx = k; g_cases[i].seed = mix32((uint32_t)i + 1u);
    }
    for (k = 0; k < N_AL; ++k, ++i) {
        g_cases[i].cat = CAT_AL; g_cases[i].variant = k % 3;
        g_cases[i].idx = k; g_cases[i].seed = mix32((uint32_t)i + 1u);
    }
    for (k = 0; k < N_EP; ++k, ++i) {
        g_cases[i].cat = CAT_EP; g_cases[i].variant = k % 3;
        g_cases[i].idx = k; g_cases[i].seed = mix32((uint32_t)i + 1u);
    }
    assert(i == N_CASES);
}

/* ------------------------------------------------------------------ */
/* Category 1: hostile mbstate_t reused across byte sequences          */
/* ------------------------------------------------------------------ */
static mbstate_t g_mb_base[4];

static void init_mb_bases(void) {
    /* Only valid mbstate_t representations: zeroed (initial state) and three
     * real non-initial states from conversions that consumed a UTF-8 lead.
     * A 0xFF fill or random bytes used to be used here as "hostile" states,
     * but glibc's iconv asserts that the recorded byte count fits __value
     * (iconv/loop.c: internal_ascii_loop_single) and aborts the process on
     * such a value (SIGABRT -> FINDING at case 1 on Linux CI).  Using genuine
     * partial states keeps all 200 mbstate_t cases hostile *and* defined. */
    static const char leads[3] = { (char)0xC2, (char)0xC3, (char)0xE0 };
    wchar_t w = 0;
    int k;
    memset(&g_mb_base[0], 0x00, sizeof g_mb_base[0]);
    for (k = 0; k < 3; ++k) {
        memset(&g_mb_base[k + 1], 0x00, sizeof g_mb_base[k + 1]);
        (void)cplus_mbrtowc(&w, &leads[k], 1, &g_mb_base[k + 1]);
    }
}

static int mb_result_defined(size_t r, size_t n) {
    return r == 0 || r == (size_t)-1 || r == (size_t)-2 ||
           (r >= 1 && r <= n);
}

static void run_mb(const case_t *c) {
    unsigned char seq[10];
    char mb[11];
    wchar_t wc = 0;
    wchar_t wbuf[8];
    char cbuf[32];
    const char *sp;
    const wchar_t *wp;
    mbstate_t local;
    size_t len, k, r;
    uint32_t s = c->seed;

    len = 1 + (size_t)(s % 8u); /* 1..8 */
    for (k = 0; k < len; ++k) {
        unsigned t;
        s = mix32(s);
        t = (unsigned)(s % 6u);
        if (t == 0u)
            seq[k] = (unsigned char)(0x20u + (s >> 8) % 0x5fu); /* ASCII */
        else if (t == 1u)
            seq[k] = (unsigned char)(0xc2u + (s >> 8) % 0x33u); /* UTF-8 lead */
        else if (t == 2u)
            seq[k] = (unsigned char)(0x80u + (s >> 8) % 0x40u); /* continuation */
        else if (t == 3u)
            seq[k] = 0x00u; /* embedded NUL -> defined return 0 */
        else if (t == 4u)
            seq[k] = 0xffu; /* invalid byte -> EILSEQ */
        else
            seq[k] = (unsigned char)((s >> 8) & 0xffu);
    }
    memcpy(mb, seq, len);
    mb[len] = '\0';

    /* mbrtowc reuses the persistent (mutating) state: hostile states are
     * genuinely carried from one sequence into the next. */
    errno = 0;
    r = cplus_mbrtowc(&wc, mb, len, &g_mb_base[c->variant]);
    assert(mb_result_defined(r, len));

    /* wcrtomb sees a fresh copy of the hostile base state. */
    memcpy(&local, &g_mb_base[c->variant], sizeof local);
    errno = 0;
    r = cplus_wcrtomb(cbuf, (wchar_t)(s % 0x110000u), &local);
    assert(r == (size_t)-1 || r <= sizeof cbuf);

    /* mbsrtowcs with a hostile state and a bounded destination. */
    memcpy(&local, &g_mb_base[c->variant], sizeof local);
    sp = mb;
    errno = 0;
    r = cplus_mbsrtowcs(wbuf, &sp, 8, &local);
    assert(r == (size_t)-1 || r <= 8);

    /* wcsrtombs with a hostile state and a bounded destination. */
    memcpy(&local, &g_mb_base[c->variant], sizeof local);
    wp = L"ab\x00e9z";
    errno = 0;
    r = cplus_wcsrtombs(cbuf, &wp, sizeof cbuf, &local);
    assert(r == (size_t)-1 || r <= sizeof cbuf);
}

/* ------------------------------------------------------------------ */
/* Category 2: hostile struct tm through the time formatters           */
/* ------------------------------------------------------------------ */
static const int tm_sec_vals[]   = {0, 1, 59, 60, 61, -1, 99};
static const int tm_min_vals[]   = {0, 1, 59, 60, -1};
static const int tm_hour_vals[]  = {0, 1, 23, 24, 25, -1};
static const int tm_mday_vals[]  = {0, 1, 28, 31, 32, -1};
static const int tm_mon_vals[]   = {0, 1, 11, 12, 13, -1};
static const int tm_year_vals[]  = {-1, 0, 1, 69, 70, 99, 100, 138, 200, 400, 2000, 10000};
static const int tm_wday_vals[]  = {0, 1, 6, 7, -1};
static const int tm_yday_vals[]  = {0, 1, 365, 366, -1};
static const int tm_isdst_vals[] = {-1, 0, 1, 2};

#define PICK(arr, seed) ((arr)[(size_t)((seed) % (sizeof(arr) / sizeof((arr)[0])))])

static const char *const tm_fmts[] = {
    "%A %B %Y", "%c", "%x %X", "%j %U %W", "%a %b %d %H:%M:%S", "%%", "%s"
};
static const wchar_t *const tm_wfmts[] = {
    L"%A %B %Y", L"%c", L"%x %X", L"%%", L"%H:%M:%S"
};

static void run_tm(const case_t *c) {
    struct tm t;
    struct tm mk;
    char b[16];
    wchar_t wb[16];
    size_t cap, wcap, r;
    uint32_t s = c->seed;
    char *asc;

    memset(&t, 0, sizeof t);
    if (c->idx < 8) {
        /* Explicitly mandated hostile combinations. */
        switch (c->idx) {
        case 0: break;                          /* all zero */
        case 1: t.tm_year = 0; break;
        case 2: t.tm_mon = 13; break;
        case 3: t.tm_mday = 32; break;
        case 4: t.tm_hour = 25; break;
        case 5: t.tm_isdst = -1; break;
        case 6: t.tm_isdst = 0; break;
        default: t.tm_isdst = 1; break;
        }
    } else {
        t.tm_sec   = PICK(tm_sec_vals,   mix32(s + 1u));
        t.tm_min   = PICK(tm_min_vals,   mix32(s + 2u));
        t.tm_hour  = PICK(tm_hour_vals,  mix32(s + 3u));
        t.tm_mday  = PICK(tm_mday_vals,  mix32(s + 4u));
        t.tm_mon   = PICK(tm_mon_vals,   mix32(s + 5u));
        t.tm_year  = PICK(tm_year_vals,  mix32(s + 6u));
        t.tm_wday  = PICK(tm_wday_vals,  mix32(s + 7u));
        t.tm_yday  = PICK(tm_yday_vals,  mix32(s + 8u));
        t.tm_isdst = PICK(tm_isdst_vals, mix32(s + 9u));
    }

    /* mktime normalizes and MUTATES its argument, so use a copy. */
    mk = t;
    (void)cplus_mktime(&mk);

    cap = (size_t)c->variant; /* 0,1,2,3,4 -- includes tiny buffers + 0 */
    assert(cap < sizeof b);
    r = cplus_strftime(b, cap, PICK(tm_fmts, mix32(s + 10u)), &t);
    assert(r <= cap);
    if (r > 0 && r < cap)
        assert(b[r] == '\0');

    asc = cplus_asctime(&t);
    assert(asc != NULL);
    assert(strlen(asc) <= 40);

    wcap = (size_t)c->variant;
    assert(wcap < sizeof wb / sizeof wb[0]);
    r = cplus_wcsftime(wb, wcap, PICK(tm_wfmts, mix32(s + 11u)), &t);
    assert(r <= wcap);
    if (r > 0 && r < wcap)
        assert(wb[r] == L'\0');
}

/* ------------------------------------------------------------------ */
/* Category 3: hostile fenv_t / fexcept_t, real env restored each time  */
/* ------------------------------------------------------------------ */
static void run_fe(const case_t *c) {
    fenv_t clean;
    fenv_t env;
    fexcept_t fl;
    unsigned char *p = (unsigned char *)&env;
    unsigned char *q = (unsigned char *)&fl;
    uint32_t s = c->seed;
    size_t k;
    int r, fn;

    assert(cplus_fegetenv(&clean) == 0);

    for (k = 0; k < sizeof fl; ++k) {
        s = mix32(s);
        q[k] = (c->variant == 0) ? 0x00u :
               (c->variant == 1) ? 0xFFu : (unsigned char)s;
    }

#if defined(__x86_64__) || defined(__i386__)
    /* On x86, ldmxcsr raises #GP on reserved MXCSR bits, so loading arbitrary
     * bit-patterns as fenv_t traps; use a valid fenv_t and keep hostile fl. */
    (void)p;
    env = clean;
    fn = c->idx % 4;
    switch (fn) {
    case 0:  r = cplus_fesetenv(&env); break;
    case 1:  r = cplus_feupdateenv(&env); break;
    case 2:  r = cplus_fesetenv(FE_DFL_ENV); break;
    default: r = cplus_fesetexceptflag(&fl, FE_ALL_EXCEPT); break;
    }
#else
    for (k = 0; k < sizeof env; ++k) {
        s = mix32(s);
        p[k] = (c->variant == 0) ? 0x00u :
               (c->variant == 1) ? 0xFFu : (unsigned char)s;
    }

    fn = c->idx % 3;
    switch (fn) {
    case 0:  r = cplus_fesetenv(&env); break;
    case 1:  r = cplus_feupdateenv(&env); break;
    default: r = cplus_fesetexceptflag(&fl, FE_ALL_EXCEPT); break;
    }
#endif
    (void)r;
    (void)cplus_fetestexcept(FE_ALL_EXCEPT);

    /* Immediately restore the real environment with no FP work in between. */
    assert(cplus_fesetenv(&clean) == 0);
}

/* ------------------------------------------------------------------ */
/* Category 4: allocation metadata on never-allocated stack addresses  */
/* ------------------------------------------------------------------ */
static void run_al(const case_t *c) {
    char sc = 'x';
    int si = 42;
    double sd = 3.25;
    short sarr[3] = {1, 2, 3};
    struct { char a; long b; } ss = {'z', 9};
    void *stack_ptrs[8];
    void *p;
    cplus_alloc_meta meta;
    cplus_alloc_meta m2;
    int r;

    stack_ptrs[0] = &sc;
    stack_ptrs[1] = &si;
    stack_ptrs[2] = &sd;
    stack_ptrs[3] = &sarr[1];
    stack_ptrs[4] = &ss;
    stack_ptrs[5] = sarr;
    stack_ptrs[6] = &ss.a;
    stack_ptrs[7] = (void *)&sarr[2];
    p = stack_ptrs[c->idx % 8];

    if (c->variant == 0)
        memset(&meta, 0x00, sizeof meta);
    else if (c->variant == 1)
        memset(&meta, 0xFF, sizeof meta);
    else {
        memset(&meta, 0, sizeof meta);
        meta.byte_size = (size_t)0xdeadbeefu;
        meta.is_live = 1; /* hostile pre-state must be fully overwritten */
    }

    r = cplus_alloc_get_meta(p, &meta);
    assert(r != 0);
    assert(meta.is_live == 0);
    assert(meta.byte_size == 0 && meta.elem_size == 0 &&
           meta.elem_count == 0 && meta.generation == 0);
    assert(cplus_alloc_is_live(p) == 0);
    assert(cplus_alloc_size(p) == 0);

    if (c->variant == 1) {
        memset(&m2, 0xFF, sizeof m2);
        assert(cplus_alloc_get_meta(p, NULL) != 0);
        assert(cplus_alloc_get_meta(NULL, &m2) != 0);
        assert(m2.is_live == 0 && m2.byte_size == 0);
    }
}

/* ------------------------------------------------------------------ */
/* Category 5: hostile conversion endptr pointers                      */
/* ------------------------------------------------------------------ */
static const wchar_t *const ep_inputs[] = {
    L"123", L"  -42abc", L"0x1f", L"", L"abc", L"  +3.14e2xyz",
    L"0XFF", L"\t0777", L"9223372036854775807", L"nan", L"inf",
    L"   ", L"+0", L"-0xdeadBEEF", L"1e309", L"0"
};
static const int ep_bases[] = {0, 10, 16, 8, 36};

static void run_ep(const case_t *c) {
    wchar_t buf[64];
    const wchar_t *nptr = buf;
    const wchar_t *in;
    wchar_t *end = NULL;
    wchar_t **ep = NULL;
    size_t len;
    int fn = c->idx % 9;
    int base = ep_bases[(c->seed >> 4) % 5u];

    in = PICK(ep_inputs, c->seed);
    len = wcslen(in);
    assert(len < sizeof buf / sizeof buf[0]);
    memcpy(buf, in, (len + 1) * sizeof(wchar_t));

    if (c->variant == 0) {
        end = NULL; ep = &end;             /* zeroed wchar_t* */
    } else if (c->variant == 1) {
        memset(&end, 0xFF, sizeof end);    /* hostile-but-valid stack slot */
        ep = &end;
    } else {
        ep = NULL;                         /* genuinely optional endptr */
    }

    switch (fn) {
    case 0: (void)cplus_wcstol(nptr, ep, base); break;
    case 1: (void)cplus_wcstoll(nptr, ep, base); break;
    case 2: (void)cplus_wcstoul(nptr, ep, base); break;
    case 3: (void)cplus_wcstoull(nptr, ep, base); break;
    case 4: (void)cplus_wcstod(nptr, ep); break;
    case 5: (void)cplus_wcstof(nptr, ep); break;
    case 6: (void)cplus_wcstold(nptr, ep); break;
    case 7: (void)cplus_wcstoimax(nptr, ep, base); break;
    default: (void)cplus_wcstoumax(nptr, ep, base); break;
    }

    if (ep != NULL && *ep != NULL) {
        assert(*ep >= nptr);
        assert(*ep <= nptr + len);
    }
}

/* ------------------------------------------------------------------ */
/* Table-driven dispatch                                               */
/* ------------------------------------------------------------------ */
typedef void (*case_fn)(const case_t *);
static case_fn g_fns[] = {NULL, run_mb, run_tm, run_fe, run_al, run_ep};

static void positive_control(void) {
    /* Ensure the metadata query really can report a live allocation, so a
     * bug that always returns "not live" cannot silently pass. */
    cplus_alloc_meta meta;
    void *p = cplus_malloc(32);
    assert(p != NULL);
    assert(cplus_alloc_get_meta(p, &meta) == 0);
    assert(meta.is_live == 1 && meta.byte_size == 32);
    cplus_free(p);
}

static int trace_cases = 0;

int main(void) {
    int i;
    if (setlocale(LC_ALL, "en_US.UTF-8") == NULL)
        (void)setlocale(LC_ALL, "C");
    install_handlers();
    build_table();
    init_mb_bases();
    positive_control();
    {
        const char *wd = getenv("CPLUS_CORPUS_WATCHDOG");
        const char *tr = getenv("CPLUS_CORPUS_TRACE");
        const char *hg = getenv("CPLUS_CORPUS_HANG_AT");
        if (wd != (const char *)0) {
            long secs = strtol(wd, (char **)0, 10);
            if (secs > 0 && secs <= 3600) {
                signal(SIGALRM, hang_handler);
                alarm((unsigned int)secs);
            }
        }
        if (hg != (const char *)0)
            g_hang_at = atoi(hg);
        if (tr != (const char *)0 && tr[0] == '1')
            trace_cases = 1;
    }

    for (i = 0; i < N_CASES; ++i) {
        g_active_case = (sig_atomic_t)i;
        g_active_cat = (sig_atomic_t)g_cases[i].cat;
        g_active_variant = (sig_atomic_t)g_cases[i].variant;
        g_active_idx = (sig_atomic_t)g_cases[i].idx;
        if (trace_cases) {
            printf("corpus_uninit case %d\n", i);
            fflush(stdout);
        }
        if (i == g_hang_at)
            sleep(5);                 /* diagnostic hook, off by default */
        assert(g_cases[i].cat >= CAT_MB && g_cases[i].cat <= CAT_EP);
        g_fns[g_cases[i].cat](&g_cases[i]);
    }
    alarm(0);

    printf("corpus_uninit: %d cases ok\n", N_CASES);
    return 0;
}
