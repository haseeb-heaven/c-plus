/* methods_string.c -- per-method unit + logging-parity tests for every string
 * operation in both ordinary-C layers and (where present) the checked APIs.
 *
 * Build (default SafeCRT layer):
 *   clang -std=c99 -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
 *         -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
 *         tests/methods/methods_string.c \
 *         build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *         -lm -pthread -o methods_string
 *
 * Build (plain hardened libc layer):
 *   clang -std=c99 -DPLAIN_LIBC -Ilibc -Ilibc/install-include \
 *         tests/methods/methods_string.c build/libc/libcplus_hardened.a \
 *         -lm -pthread -o methods_string_plain
 *
 * Each method's whole scenario battery is executed twice: once with logging
 * OFF (correctness assertions active) and once with logging ON (assertions
 * inactive, observational snapshot only).  The two snapshots must match, so
 * every scenario is checked for logging ON/OFF parity.  Canary bytes surround
 * every stack destination; they are snapshotted and asserted on each scenario.
 * No genuine UB is provoked: NULL is only passed to guarded entry points,
 * unterminated sources are only used with an explicit bound where the callee
 * is bounded, and overlap is only exercised through an API that defines it.
 */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>

#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#define MS_HAS_CHECKED 0
static void ms_log_off(void) { cplus_libc_log_set_enabled(0); }
static void ms_log_on(void)  { cplus_libc_log_set_enabled(1); }
#else
#include "safe_c.h"
#include "safe_runtime.h"
#include "safe_log.h"
#define MS_HAS_CHECKED 1
static void ms_log_off(void) { cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF); }
static void ms_log_on(void)  { cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE); }
#endif

/* strdup/strndup are host-C calls in both layers (not remapped); match them
 * with the host deallocator rather than the layer's tracked free wrapper. */
#undef free

/* ------------------------------------------------------------------ output */
static void ms_puts(const char *s) { (void)fputs(s, stdout); }
static void ms_num(unsigned long long v) {
    char t[32];
    int i = 0;
    if (v == 0) t[i++] = '0';
    while (v) { t[i++] = (char)('0' + (int)(v % 10ULL)); v /= 10ULL; }
    while (i > 0) (void)fputc(t[--i], stdout);
}

/* --------------------------------------------------------------- framework */
static const char *g_method = "";
static const char *g_scen = "";
static int g_validate = 0;
static unsigned long g_checks = 0;
static unsigned long g_fails = 0;
static unsigned long g_skips = 0;

static void ms_skip(const char *m, const char *sc, const char *why) {
    g_skips++;
    /* The battery runs twice (logging off/on); report each skip only once. */
    if (!g_validate) return;
    ms_puts("SKIP "); ms_puts(m); ms_puts(" "); ms_puts(sc);
    ms_puts(": "); ms_puts(why); ms_puts("\n");
}

#define SC(x) (g_scen = (x))
#define CHK(c) do {                                                            \
    if (g_validate) {                                                          \
        g_checks++;                                                            \
        if (!(c)) {                                                            \
            g_fails++;                                                         \
            ms_puts("FINDING "); ms_puts(g_method); ms_puts(" ");              \
            ms_puts(g_scen); ms_puts(": " #c "\n");                            \
        }                                                                      \
    }                                                                          \
} while (0)

#define MS_MIX(s, v) ((s) = ((s) ^ (unsigned long long)(v)) * 1099511628211ULL)

typedef struct {
    unsigned long long sum;
    long r;                 /* primary integer result / status code */
    int err;                /* errno after the call */
    int ptr;                /* pointer result: -1 NULL, >=0 offset, -2 other */
    int canary;             /* 1 = canaries intact */
    unsigned char dbuf[48]; /* snapshot of destination bytes */
} snap_t;

static void snap_init(snap_t *s) {
    size_t i;
    s->sum = 1469598103934665603ULL;
    s->r = 0; s->err = 0; s->ptr = -2; s->canary = 1;
    for (i = 0; i < sizeof s->dbuf; i++) s->dbuf[i] = 0;
}
static void snap_r(snap_t *s, long v)   { MS_MIX(s->sum, (unsigned long long)v); s->r = v; }
static void snap_err(snap_t *s, int e)  { MS_MIX(s->sum, (unsigned long long)(unsigned)e); s->err = e; }
static void snap_ptr(snap_t *s, int p)  { MS_MIX(s->sum, (unsigned long long)(unsigned)(p + 2)); s->ptr = p; }
static void snap_bytes(snap_t *s, const unsigned char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        MS_MIX(s->sum, b[i]);
        if (i < sizeof s->dbuf) s->dbuf[i] = b[i];
    }
}
static int snap_eq(const snap_t *a, const snap_t *b) {
    size_t i;
    if (a->sum != b->sum || a->r != b->r || a->err != b->err ||
        a->ptr != b->ptr || a->canary != b->canary) return 0;
    for (i = 0; i < sizeof a->dbuf; i++)
        if (a->dbuf[i] != b->dbuf[i]) return 0;
    return 1;
}

/* ------------------------------------------------------------ canary buffer */
static const unsigned char MS_C[8] = {0xA5,0x5A,0xC3,0x3C,0x3C,0xC3,0x5A,0xA5};
typedef struct { unsigned char pre[8]; char b[40]; unsigned char post[8]; } cb_t;
static void cb_init(cb_t *c, int ch) {
    size_t i;
    for (i = 0; i < 8; i++) { c->pre[i] = MS_C[i]; c->post[i] = MS_C[i]; }
    for (i = 0; i < 40; i++) c->b[i] = (char)ch;
}
static void cb_set(cb_t *c, const char *str) {
    size_t i, n = strlen(str);
    cb_init(c, 0);
    for (i = 0; i < n && i < 40; i++) c->b[i] = str[i];
}
static int cb_ok(const cb_t *c) {
    size_t i;
    for (i = 0; i < 8; i++)
        if (c->pre[i] != MS_C[i] || c->post[i] != MS_C[i]) return 0;
    return 1;
}
static void snap_canary(snap_t *s, const cb_t *c) {
    int ok = cb_ok(c);
    MS_MIX(s->sum, (unsigned)ok);
    s->canary = ok;
    CHK(ok);
}
static void filln(char *p, size_t n, char ch) {
    size_t i;
    for (i = 0; i < n; i++) p[i] = ch;
    p[n] = 0;
}
static int offof(const char *base, const char *p) {
    return p ? (int)(p - base) : -1;
}
static void snap_dest(snap_t *s, cb_t *c) {
    snap_bytes(s, (const unsigned char *)c->b, 40);
    snap_canary(s, c);
}

typedef snap_t (*ms_fn)(void);
static void run_method(const char *name, ms_fn fn) {
    snap_t a, b;
    g_method = name;
    ms_log_off(); g_validate = 1; a = fn();
    ms_log_on();  g_validate = 0; b = fn();
    ms_log_off(); g_validate = 0;
    g_checks++;
    if (!snap_eq(&a, &b)) {
        g_fails++;
        ms_puts("FINDING "); ms_puts(name);
        ms_puts(" logging-parity: off/on results differ\n");
    }
}

/* ========================================================================= */
/* METHOD: strcpy / strcpy_s / strcpy_s_auto                                  */
static snap_t m_str_01_strcpy(void) {
    snap_t s; cb_t c; char src[64]; char *r;
    snap_init(&s);

    SC("normal");
    cb_init(&c, 0); errno = 0;
    r = strcpy(c.b, "hello");
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "hello") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty string");
    cb_init(&c, 'X'); errno = 0;
    r = strcpy(c.b, "");
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(c.b[0] == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("exact capacity");
    cb_init(&c, 0); filln(src, 39, 'a'); errno = 0;
    r = strcpy(c.b, src);
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(c.b[39] == 0); CHK(c.b[0] == 'a');
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("one over capacity");
    cb_init(&c, 'Z'); filln(src, 40, 'b'); errno = 0;
    r = strcpy(c.b, src);
    snap_ptr(&s, r == NULL ? -1 : (r == c.b ? 0 : -2));
    CHK(r == NULL); CHK(cb_ok(&c));
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("null destination");
    errno = 0; r = strcpy(NULL, "x");
    snap_ptr(&s, r == NULL ? -1 : -2); CHK(r == NULL);
    snap_err(&s, errno);

    SC("null source");
    cb_init(&c, 'Q'); errno = 0; r = strcpy(c.b, NULL);
    snap_ptr(&s, r == NULL ? -1 : -2); CHK(r == NULL); CHK(cb_ok(&c));
    snap_err(&s, errno); snap_dest(&s, &c);

#if MS_HAS_CHECKED
    SC("auto strcpy_s_auto normal");
    cb_init(&c, 0); errno = 0;
    r = cplus_strcpy_s_auto(c.b, "abc", 40);
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "abc") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("auto strcpy_s_auto one over known dmax");
    cb_init(&c, 'Q'); errno = 0;
    r = cplus_strcpy_s_auto(c.b, "abc", 3);
    snap_ptr(&s, r == NULL ? -1 : -2); CHK(r == NULL); CHK(cb_ok(&c));
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("checked strcpy_s normal");
    cb_init(&c, 0); errno = 0;
    { errno_t e = strcpy_s(c.b, (rsize_t)sizeof c.b, "abc");
      snap_r(&s, (long)e); CHK(e == 0); CHK(strcmp(c.b, "abc") == 0);
      snap_err(&s, errno); snap_dest(&s, &c); }
    SC("checked strcpy_s null dest");
    { errno_t e = cplus_strcpy_s(NULL, 16, "a"); snap_r(&s, (long)e); CHK(e != 0); }
    SC("checked strcpy_s null src");
    cb_init(&c, 'Q');
    { errno_t e = cplus_strcpy_s(c.b, 40, NULL); snap_r(&s, (long)e); CHK(e != 0);
      snap_dest(&s, &c); }
    SC("checked strcpy_s zero dmax");
    { errno_t e = cplus_strcpy_s(c.b, 0, "a"); snap_r(&s, (long)e); CHK(e != 0); }
    SC("checked strcpy_s one over dmax");
    { errno_t e = cplus_strcpy_s(c.b, 3, "abc"); snap_r(&s, (long)e); CHK(e != 0); }
#else
    ms_skip("m_str_01_strcpy", "auto/checked strcpy_s APIs", "absent in PLAIN_LIBC");
#endif
    return s;
}

/* ========================================================================= */
/* METHOD: strncpy / strncpy_s_auto                                           */
static snap_t m_str_02_strncpy(void) {
    snap_t s; cb_t c; char *r;
    snap_init(&s);
    (void)strlen("");

    SC("normal");
    cb_init(&c, 0); errno = 0;
    r = strncpy(c.b, "abcdef", 3);
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(memcmp(c.b, "abc", 3) == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty string");
    cb_init(&c, 'X'); errno = 0;
    r = strncpy(c.b, "", 5);
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(c.b[0] == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("zero length");
    cb_set(&c, "abcdef"); errno = 0;
    r = strncpy(c.b, "zzz", 0);
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "abcdef") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("unterminated source with explicit bound");
    { char u[6] = {'a','b','c','d','e','f'};
      cb_init(&c, 0); errno = 0;
      r = strncpy(c.b, u, 4);
      snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
      CHK(r == c.b); CHK(memcmp(c.b, "abcd", 4) == 0); CHK(cb_ok(&c));
      snap_err(&s, errno); snap_dest(&s, &c); }

    SC("null destination");
    errno = 0; r = strncpy(NULL, "a", 1);
    snap_ptr(&s, r == NULL ? -1 : -2); CHK(r == NULL);
    snap_err(&s, errno);

    SC("null source");
    cb_init(&c, 'Q'); errno = 0; r = strncpy(c.b, NULL, 1);
    snap_ptr(&s, r == NULL ? -1 : -2); CHK(r == NULL); CHK(cb_ok(&c));
    snap_err(&s, errno); snap_dest(&s, &c);

#if MS_HAS_CHECKED
    SC("auto strncpy_s_auto n=0");
    cb_set(&c, "abc"); errno = 0;
    r = cplus_strncpy_s_auto(c.b, "zz", 0, 40);
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "abc") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("auto strncpy_s_auto one over known dmax");
    cb_init(&c, 'Q'); errno = 0;
    r = cplus_strncpy_s_auto(c.b, "abcdef", 40, 3);
    snap_ptr(&s, r == NULL ? -1 : -2); CHK(r == NULL); CHK(cb_ok(&c));
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("checked strncpy_s normal");
    cb_init(&c, 0); errno = 0;
    { errno_t e = strncpy_s(c.b, (rsize_t)sizeof c.b, "abc", 3);
      snap_r(&s, (long)e); CHK(e == 0); CHK(strcmp(c.b, "abc") == 0);
      snap_err(&s, errno); snap_dest(&s, &c); }
    SC("checked strncpy_s null dest");
    { errno_t e = cplus_strncpy_s(NULL, 16, "a", 1); snap_r(&s, (long)e); CHK(e != 0); }
    SC("checked strncpy_s null src");
    cb_init(&c, 'Q');
    { errno_t e = cplus_strncpy_s(c.b, 40, NULL, 1); snap_r(&s, (long)e); CHK(e != 0);
      snap_dest(&s, &c); }
    SC("checked strncpy_s zero dmax");
    { errno_t e = cplus_strncpy_s(c.b, 0, "a", 1); snap_r(&s, (long)e); CHK(e != 0); }
#else
    ms_skip("m_str_02_strncpy", "auto/checked strncpy_s APIs", "absent in PLAIN_LIBC");
#endif
    return s;
}

/* ========================================================================= */
/* METHOD: strcat / strcat_s_auto                                             */
static snap_t m_str_03_strcat(void) {
    snap_t s; cb_t c; char *r;
    snap_init(&s);

    SC("normal");
    cb_set(&c, "ab"); errno = 0;
    r = strcat(c.b, "cd");
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "abcd") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty string");
    cb_set(&c, "ab"); errno = 0;
    r = strcat(c.b, "");
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "ab") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty destination");
    cb_init(&c, 0); errno = 0;
    r = strcat(c.b, "x");
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "x") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("exact capacity");
    cb_init(&c, 0); filln(c.b, 39, 'a'); errno = 0;
    r = strcat(c.b, "");
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(c.b[39] == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("one over capacity");
    cb_init(&c, 0); filln(c.b, 39, 'a'); errno = 0;
    r = strcat(c.b, "b");
    snap_ptr(&s, r == NULL ? -1 : (r == c.b ? 0 : -2));
    CHK(r == NULL); CHK(cb_ok(&c));
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("null destination");
    errno = 0; r = strcat(NULL, "a");
    snap_ptr(&s, r == NULL ? -1 : -2); CHK(r == NULL);
    snap_err(&s, errno);

    SC("null source");
    cb_set(&c, "ab"); errno = 0; r = strcat(c.b, NULL);
    snap_ptr(&s, r == NULL ? -1 : -2); CHK(r == NULL); CHK(cb_ok(&c));
    snap_err(&s, errno); snap_dest(&s, &c);

#if MS_HAS_CHECKED
    SC("auto strcat_s_auto normal");
    cb_set(&c, "ab"); errno = 0;
    r = cplus_strcat_s_auto(c.b, "cd", 40);
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "abcd") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("checked strcat_s normal");
    cb_set(&c, "ab"); errno = 0;
    { errno_t e = strcat_s(c.b, (rsize_t)sizeof c.b, "cd");
      snap_r(&s, (long)e); CHK(e == 0); CHK(strcmp(c.b, "abcd") == 0);
      snap_err(&s, errno); snap_dest(&s, &c); }
    SC("checked strcat_s null dest");
    { errno_t e = cplus_strcat_s(NULL, 16, "a"); snap_r(&s, (long)e); CHK(e != 0); }
    SC("checked strcat_s null src");
    cb_set(&c, "ab");
    { errno_t e = cplus_strcat_s(c.b, 40, NULL); snap_r(&s, (long)e); CHK(e != 0);
      snap_dest(&s, &c); }
    SC("checked strcat_s zero dmax");
    { errno_t e = cplus_strcat_s(c.b, 0, "a"); snap_r(&s, (long)e); CHK(e != 0); }
    SC("checked strcat_s overlap defined rejection");
    cb_set(&c, "abc");
    { errno_t e = cplus_strcat_s(c.b, 40, c.b); snap_r(&s, (long)e); CHK(e != 0);
      snap_dest(&s, &c); }
#else
    ms_skip("m_str_03_strcat", "auto/checked strcat_s APIs", "absent in PLAIN_LIBC");
#endif
    return s;
}

/* ========================================================================= */
/* METHOD: strncat / strncat_s_auto                                           */
static snap_t m_str_04_strncat(void) {
    snap_t s; cb_t c; char *r;
    snap_init(&s);

    SC("normal");
    cb_set(&c, "ab"); errno = 0;
    r = strncat(c.b, "cdef", 2);
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "abcd") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("zero length");
    cb_set(&c, "ab"); errno = 0;
    r = strncat(c.b, "zz", 0);
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "ab") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty source");
    cb_set(&c, "ab"); errno = 0;
    r = strncat(c.b, "", 5);
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "ab") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("n greater than source length");
    cb_set(&c, "ab"); errno = 0;
    r = strncat(c.b, "xy", 10);
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "abxy") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("one over capacity");
    cb_init(&c, 0); filln(c.b, 39, 'a'); errno = 0;
    r = strncat(c.b, "xy", 2);
    snap_ptr(&s, r == NULL ? -1 : (r == c.b ? 0 : -2));
    CHK(r == NULL); CHK(cb_ok(&c));
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("null destination");
    errno = 0; r = strncat(NULL, "a", 1);
    snap_ptr(&s, r == NULL ? -1 : -2); CHK(r == NULL);
    snap_err(&s, errno);

    SC("null source");
    cb_set(&c, "ab"); errno = 0; r = strncat(c.b, NULL, 1);
    snap_ptr(&s, r == NULL ? -1 : -2); CHK(r == NULL); CHK(cb_ok(&c));
    snap_err(&s, errno); snap_dest(&s, &c);

    ms_skip("m_str_04_strncat", "unterminated source via ordinary strncat",
            "bounded source unsafe for strncat in PLAIN_LIBC layer");

#if MS_HAS_CHECKED
    SC("auto strncat_s_auto n=0");
    cb_set(&c, "ab"); errno = 0;
    r = cplus_strncat_s_auto(c.b, "zz", 0, 40);
    snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
    CHK(r == c.b); CHK(strcmp(c.b, "ab") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("auto strncat_s_auto unterminated source bound");
    { char u[6] = {'a','b','c','d','e','f'};
      cb_set(&c, "X"); errno = 0;
      r = cplus_strncat_s_auto(c.b, u, 4, 40);
      snap_ptr(&s, r == c.b ? 0 : (r ? -2 : -1));
      CHK(r == c.b); CHK(memcmp(c.b + 1, "abcd", 4) == 0); CHK(cb_ok(&c));
      snap_err(&s, errno); snap_dest(&s, &c); }

    SC("checked strncat_s normal");
    cb_set(&c, "ab"); errno = 0;
    { errno_t e = strncat_s(c.b, (rsize_t)sizeof c.b, "cd", 2);
      snap_r(&s, (long)e); CHK(e == 0); CHK(strcmp(c.b, "abcd") == 0);
      snap_err(&s, errno); snap_dest(&s, &c); }
    SC("checked strncat_s null dest");
    { errno_t e = cplus_strncat_s(NULL, 16, "a", 1); snap_r(&s, (long)e); CHK(e != 0); }
    SC("checked strncat_s null src");
    cb_set(&c, "ab");
    { errno_t e = cplus_strncat_s(c.b, 40, NULL, 1); snap_r(&s, (long)e); CHK(e != 0);
      snap_dest(&s, &c); }
    SC("checked strncat_s zero dmax");
    { errno_t e = cplus_strncat_s(c.b, 0, "a", 1); snap_r(&s, (long)e); CHK(e != 0); }
    SC("checked strncat_s overlap defined rejection");
    cb_set(&c, "abc");
    { errno_t e = cplus_strncat_s(c.b, 40, c.b, 3); snap_r(&s, (long)e); CHK(e != 0);
      snap_dest(&s, &c); }
#else
    ms_skip("m_str_04_strncat", "auto/checked strncat_s APIs", "absent in PLAIN_LIBC");
#endif
    return s;
}

/* ========================================================================= */
/* METHOD: strlen                                                             */
static snap_t m_str_05_strlen(void) {
    snap_t s; cb_t c; size_t n;
    snap_init(&s);

    SC("normal");
    cb_set(&c, "hello"); errno = 0; n = strlen(c.b);
    snap_r(&s, (long)n); CHK(n == 5);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty string");
    cb_set(&c, ""); errno = 0; n = strlen(c.b);
    snap_r(&s, (long)n); CHK(n == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("exact capacity length");
    cb_init(&c, 0); filln(c.b, 39, 'a'); errno = 0; n = strlen(c.b);
    snap_r(&s, (long)n); CHK(n == 39);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("embedded NUL");
    cb_init(&c, 0); c.b[0]='a'; c.b[1]='b'; c.b[2]=0; c.b[3]='z'; errno=0;
    n = strlen(c.b);
    snap_r(&s, (long)n); CHK(n == 2);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("literal");
    errno = 0; n = strlen("abcdef");
    snap_r(&s, (long)n); CHK(n == 6); snap_err(&s, errno);
    ms_skip("m_str_05_strlen", "NULL / unterminated",
            "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strnlen / strnlen_s                                                */
static snap_t m_str_06_strnlen(void) {
    snap_t s; cb_t c; size_t n;
    snap_init(&s);

    SC("normal within bound");
    cb_set(&c, "abc"); errno = 0; n = strnlen(c.b, 10);
    snap_r(&s, (long)n); CHK(n == 3);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty string");
    cb_set(&c, ""); errno = 0; n = strnlen(c.b, 10);
    snap_r(&s, (long)n); CHK(n == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("zero length");
    cb_set(&c, "abc"); errno = 0; n = strnlen(c.b, 0);
    snap_r(&s, (long)n); CHK(n == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("bound smaller than string");
    cb_set(&c, "abc"); errno = 0; n = strnlen(c.b, 2);
    snap_r(&s, (long)n); CHK(n == 2);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("exact bound");
    cb_set(&c, "abc"); errno = 0; n = strnlen(c.b, 3);
    snap_r(&s, (long)n); CHK(n == 3);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("unterminated source with explicit bound");
    { char u[4] = {'a','b','c','d'}; errno = 0; n = strnlen(u, 4);
      snap_r(&s, (long)n); CHK(n == 4); snap_err(&s, errno); }

#if MS_HAS_CHECKED
    SC("checked strnlen_s normal");
    errno = 0; n = strnlen_s("abc", 10);
    snap_r(&s, (long)n); CHK(n == 3); snap_err(&s, errno);
    SC("checked strnlen_s zero smax");
    n = strnlen_s("abc", 0); snap_r(&s, (long)n); CHK(n == 0);
    SC("checked strnlen_s NULL guarded");
    n = strnlen_s(NULL, 10); snap_r(&s, (long)n); CHK(n == 0);
#else
    ms_skip("m_str_06_strnlen", "checked strnlen_s API", "absent in PLAIN_LIBC");
#endif
    ms_skip("m_str_06_strnlen", "raw strnlen NULL", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strcmp / strcmp_s                                                  */
static snap_t m_str_07_strcmp(void) {
    snap_t s; cb_t a, b; int v;
    snap_init(&s);

    SC("equal");
    cb_set(&a, "abc"); cb_set(&b, "abc"); errno = 0; v = strcmp(a.b, b.b);
    snap_r(&s, v); CHK(v == 0); snap_err(&s, errno);

    SC("less than");
    cb_set(&a, "abc"); cb_set(&b, "abd"); errno = 0; v = strcmp(a.b, b.b);
    snap_r(&s, v); CHK(v < 0); snap_err(&s, errno);

    SC("greater than");
    cb_set(&a, "abd"); cb_set(&b, "abc"); errno = 0; v = strcmp(a.b, b.b);
    snap_r(&s, v); CHK(v > 0); snap_err(&s, errno);

    SC("empty strings");
    cb_set(&a, ""); cb_set(&b, ""); errno = 0; v = strcmp(a.b, b.b);
    snap_r(&s, v); CHK(v == 0); snap_err(&s, errno);

    SC("prefix");
    cb_set(&a, "ab"); cb_set(&b, "abc"); errno = 0; v = strcmp(a.b, b.b);
    snap_r(&s, v); CHK(v < 0); snap_err(&s, errno);

    SC("embedded NUL");
    cb_set(&a, "ab"); cb_set(&b, "ab");
    a.b[2]=0; a.b[3]='z'; b.b[2]=0; b.b[3]='y'; errno = 0; v = strcmp(a.b, b.b);
    snap_r(&s, v); CHK(v == 0); snap_err(&s, errno);

#if MS_HAS_CHECKED
    SC("checked strcmp_s equal");
    { int ind = -1; errno_t e = cplus_strcmp_s("abc", 4, "abc", &ind);
      snap_r(&s, (long)e); CHK(e == 0); CHK(ind == 0); }
    SC("checked strcmp_s less");
    { int ind = 0; errno_t e = cplus_strcmp_s("abc", 4, "abd", &ind);
      snap_r(&s, (long)e); CHK(e == 0); CHK(ind < 0); }
    SC("checked strcmp_s greater");
    { int ind = 0; errno_t e = cplus_strcmp_s("abd", 4, "abc", &ind);
      snap_r(&s, (long)e); CHK(e == 0); CHK(ind > 0); }
    SC("checked strcmp_s NULL dest");
    { int ind = 0; errno_t e = cplus_strcmp_s(NULL, 4, "a", &ind);
      snap_r(&s, (long)e); CHK(e != 0); }
    SC("checked strcmp_s NULL src");
    { int ind = 0; errno_t e = cplus_strcmp_s("a", 4, NULL, &ind);
      snap_r(&s, (long)e); CHK(e != 0); }
    SC("checked strcmp_s NULL indicator");
    { errno_t e = cplus_strcmp_s("a", 4, "a", NULL);
      snap_r(&s, (long)e); CHK(e != 0); }
#else
    ms_skip("m_str_07_strcmp", "checked strcmp_s API", "absent in PLAIN_LIBC");
#endif
    ms_skip("m_str_07_strcmp", "raw strcmp NULL", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strncmp                                                            */
static snap_t m_str_08_strncmp(void) {
    snap_t s; cb_t a, b; int v;
    snap_init(&s);

    SC("equal within n");
    cb_set(&a, "abcdef"); cb_set(&b, "abcxyz"); errno = 0; v = strncmp(a.b, b.b, 3);
    snap_r(&s, v); CHK(v == 0); snap_err(&s, errno);

    SC("zero length");
    cb_set(&a, "abc"); cb_set(&b, "xyz"); errno = 0; v = strncmp(a.b, b.b, 0);
    snap_r(&s, v); CHK(v == 0); snap_err(&s, errno);

    SC("difference inside n");
    cb_set(&a, "abc"); cb_set(&b, "abd"); errno = 0; v = strncmp(a.b, b.b, 3);
    snap_r(&s, v); CHK(v < 0); snap_err(&s, errno);

    SC("difference beyond n");
    cb_set(&a, "abc"); cb_set(&b, "abd"); errno = 0; v = strncmp(a.b, b.b, 2);
    snap_r(&s, v); CHK(v == 0); snap_err(&s, errno);

    SC("empty strings");
    cb_set(&a, ""); cb_set(&b, ""); errno = 0; v = strncmp(a.b, b.b, 5);
    snap_r(&s, v); CHK(v == 0); snap_err(&s, errno);

    SC("n larger than strings");
    cb_set(&a, "ab"); cb_set(&b, "ab"); errno = 0; v = strncmp(a.b, b.b, 100);
    snap_r(&s, v); CHK(v == 0); snap_err(&s, errno);
    ms_skip("m_str_08_strncmp", "NULL", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strchr                                                             */
static snap_t m_str_09_strchr(void) {
    snap_t s; cb_t c; char *p;
    snap_init(&s);

    SC("found first occurrence");
    cb_set(&c, "banana"); errno = 0; p = strchr(c.b, 'a');
    snap_ptr(&s, offof(c.b, p)); CHK(p == c.b + 1); CHK(*p == 'a'); snap_err(&s, errno);
    snap_dest(&s, &c);

    SC("not found");
    cb_set(&c, "banana"); errno = 0; p = strchr(c.b, 'z');
    snap_ptr(&s, -1); CHK(p == NULL); snap_err(&s, errno);

    SC("find terminator");
    cb_set(&c, "abc"); errno = 0; p = strchr(c.b, 0);
    snap_ptr(&s, offof(c.b, p)); CHK(p == c.b + 3); snap_err(&s, errno);

    SC("empty string");
    cb_set(&c, ""); errno = 0; p = strchr(c.b, 'a');
    snap_ptr(&s, -1); CHK(p == NULL); snap_err(&s, errno);

    SC("single char");
    cb_set(&c, "x"); errno = 0; p = strchr(c.b, 'x');
    snap_ptr(&s, offof(c.b, p)); CHK(p == c.b); snap_err(&s, errno);
    ms_skip("m_str_09_strchr", "NULL", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strrchr                                                            */
static snap_t m_str_10_strrchr(void) {
    snap_t s; cb_t c; char *p;
    snap_init(&s);

    SC("found last occurrence");
    cb_set(&c, "banana"); errno = 0; p = strrchr(c.b, 'a');
    snap_ptr(&s, offof(c.b, p)); CHK(p == c.b + 5); CHK(*p == 'a'); snap_err(&s, errno);
    snap_dest(&s, &c);

    SC("not found");
    cb_set(&c, "banana"); errno = 0; p = strrchr(c.b, 'z');
    snap_ptr(&s, -1); CHK(p == NULL); snap_err(&s, errno);

    SC("find terminator");
    cb_set(&c, "abc"); errno = 0; p = strrchr(c.b, 0);
    snap_ptr(&s, offof(c.b, p)); CHK(p == c.b + 3); snap_err(&s, errno);

    SC("empty string");
    cb_set(&c, ""); errno = 0; p = strrchr(c.b, 'a');
    snap_ptr(&s, -1); CHK(p == NULL); snap_err(&s, errno);

    SC("single char");
    cb_set(&c, "x"); errno = 0; p = strrchr(c.b, 'x');
    snap_ptr(&s, offof(c.b, p)); CHK(p == c.b); snap_err(&s, errno);
    ms_skip("m_str_10_strrchr", "NULL", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strstr                                                             */
static snap_t m_str_11_strstr(void) {
    snap_t s; cb_t h; char *p;
    snap_init(&s);

    SC("found middle");
    cb_set(&h, "hello world"); errno = 0; p = strstr(h.b, "wor");
    snap_ptr(&s, offof(h.b, p)); CHK(p == h.b + 6); snap_err(&s, errno); snap_dest(&s, &h);

    SC("not found");
    cb_set(&h, "hello world"); errno = 0; p = strstr(h.b, "xyz");
    snap_ptr(&s, -1); CHK(p == NULL); snap_err(&s, errno);

    SC("empty needle");
    cb_set(&h, "abc"); errno = 0; p = strstr(h.b, "");
    snap_ptr(&s, offof(h.b, p)); CHK(p == h.b); snap_err(&s, errno);

    SC("whole string");
    cb_set(&h, "abc"); errno = 0; p = strstr(h.b, "abc");
    snap_ptr(&s, offof(h.b, p)); CHK(p == h.b); snap_err(&s, errno);

    SC("needle longer than haystack");
    cb_set(&h, "ab"); errno = 0; p = strstr(h.b, "abcdef");
    snap_ptr(&s, -1); CHK(p == NULL); snap_err(&s, errno);
    ms_skip("m_str_11_strstr", "NULL", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strcasestr                                                         */
static snap_t m_str_12_strcasestr(void) {
    snap_t s; cb_t h; char *p;
    snap_init(&s);

    SC("found case-insensitive");
    cb_set(&h, "Hello WORLD"); errno = 0; p = strcasestr(h.b, "world");
    snap_ptr(&s, offof(h.b, p)); CHK(p == h.b + 6); snap_err(&s, errno); snap_dest(&s, &h);

    SC("not found");
    cb_set(&h, "hello"); errno = 0; p = strcasestr(h.b, "xyz");
    snap_ptr(&s, -1); CHK(p == NULL); snap_err(&s, errno);

    SC("empty needle");
    cb_set(&h, "abc"); errno = 0; p = strcasestr(h.b, "");
    snap_ptr(&s, offof(h.b, p)); CHK(p == h.b); snap_err(&s, errno);

    SC("whole string mixed case");
    cb_set(&h, "AbC"); errno = 0; p = strcasestr(h.b, "abc");
    snap_ptr(&s, offof(h.b, p)); CHK(p == h.b); snap_err(&s, errno);

    SC("empty haystack");
    cb_set(&h, ""); errno = 0; p = strcasestr(h.b, "x");
    snap_ptr(&s, -1); CHK(p == NULL); snap_err(&s, errno);
    ms_skip("m_str_12_strcasestr", "NULL", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strdup                                                             */
static snap_t m_str_13_strdup(void) {
    snap_t s; cb_t c; char *d;
    snap_init(&s);

    SC("normal");
    cb_set(&c, "hello"); errno = 0; d = strdup(c.b);
    snap_ptr(&s, d ? 0 : -1); CHK(d != NULL);
    if (d) { CHK(strcmp(d, "hello") == 0); snap_r(&s, (long)strlen(d)); free(d); }
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty string");
    cb_set(&c, ""); errno = 0; d = strdup(c.b);
    snap_ptr(&s, d ? 0 : -1); CHK(d != NULL);
    if (d) { CHK(strcmp(d, "") == 0); snap_r(&s, (long)strlen(d)); free(d); }
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("long string");
    cb_init(&c, 0); filln(c.b, 39, 'q'); errno = 0; d = strdup(c.b);
    snap_ptr(&s, d ? 0 : -1); CHK(d != NULL);
    if (d) { CHK(strcmp(d, c.b) == 0); snap_r(&s, (long)strlen(d)); free(d); }
    snap_err(&s, errno); snap_dest(&s, &c);
    ms_skip("m_str_13_strdup", "NULL", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strndup                                                            */
static snap_t m_str_14_strndup(void) {
    snap_t s; cb_t c; char *d;
    snap_init(&s);

    SC("normal");
    cb_set(&c, "abcdef"); errno = 0; d = strndup(c.b, 3);
    snap_ptr(&s, d ? 0 : -1); CHK(d != NULL);
    if (d) { CHK(strcmp(d, "abc") == 0); snap_r(&s, (long)strlen(d)); free(d); }
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("zero length");
    cb_set(&c, "abcdef"); errno = 0; d = strndup(c.b, 0);
    snap_ptr(&s, d ? 0 : -1); CHK(d != NULL);
    if (d) { CHK(strcmp(d, "") == 0); snap_r(&s, (long)strlen(d)); free(d); }
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("n greater than source length");
    cb_set(&c, "abc"); errno = 0; d = strndup(c.b, 50);
    snap_ptr(&s, d ? 0 : -1); CHK(d != NULL);
    if (d) { CHK(strcmp(d, "abc") == 0); snap_r(&s, (long)strlen(d)); free(d); }
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty string");
    cb_set(&c, ""); errno = 0; d = strndup(c.b, 5);
    snap_ptr(&s, d ? 0 : -1); CHK(d != NULL);
    if (d) { CHK(strcmp(d, "") == 0); snap_r(&s, (long)strlen(d)); free(d); }
    snap_err(&s, errno); snap_dest(&s, &c);
    ms_skip("m_str_14_strndup", "NULL", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strtok                                                             */
static snap_t m_str_15_strtok(void) {
    snap_t s; cb_t c; char *t;
    snap_init(&s);

    SC("normal tokenization");
    cb_set(&c, "aa,bb,cc"); errno = 0;
    t = strtok(c.b, ",");
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "aa") == 0);
    snap_err(&s, errno);
    t = strtok(NULL, ",");
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "bb") == 0);
    t = strtok(NULL, ",");
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "cc") == 0);
    t = strtok(NULL, ",");
    snap_ptr(&s, -1); CHK(t == NULL);
    snap_dest(&s, &c);

    SC("no delimiters");
    cb_set(&c, "single"); errno = 0;
    t = strtok(c.b, ",");
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "single") == 0);
    t = strtok(NULL, ",");
    snap_ptr(&s, -1); CHK(t == NULL);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("leading delimiters");
    cb_set(&c, ",,x"); errno = 0;
    t = strtok(c.b, ",");
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "x") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty string");
    cb_set(&c, ""); errno = 0;
    t = strtok(c.b, ",");
    snap_ptr(&s, -1); CHK(t == NULL); snap_err(&s, errno); snap_dest(&s, &c);
    ms_skip("m_str_15_strtok", "NULL delim/literal input",
            "undefined by ISO C / strtok writes into its input - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strtok_r                                                           */
static snap_t m_str_16_strtok_r(void) {
    snap_t s; cb_t c; char *save = NULL; char *t;
    snap_init(&s);

    SC("normal tokenization");
    cb_set(&c, "a b c"); errno = 0;
    t = strtok_r(c.b, " ", &save);
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "a") == 0);
    snap_err(&s, errno);
    t = strtok_r(NULL, " ", &save);
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "b") == 0);
    t = strtok_r(NULL, " ", &save);
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "c") == 0);
    t = strtok_r(NULL, " ", &save);
    snap_ptr(&s, -1); CHK(t == NULL);
    snap_dest(&s, &c);

    SC("no delimiters");
    save = NULL; cb_set(&c, "solo"); errno = 0;
    t = strtok_r(c.b, ",", &save);
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "solo") == 0);
    t = strtok_r(NULL, ",", &save);
    snap_ptr(&s, -1); CHK(t == NULL);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("trailing delimiters");
    save = NULL; cb_set(&c, "x,"); errno = 0;
    t = strtok_r(c.b, ",", &save);
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "x") == 0);
    t = strtok_r(NULL, ",", &save);
    snap_ptr(&s, -1); CHK(t == NULL);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty string");
    save = NULL; cb_set(&c, ""); errno = 0;
    t = strtok_r(c.b, ",", &save);
    snap_ptr(&s, -1); CHK(t == NULL); snap_err(&s, errno); snap_dest(&s, &c);
    ms_skip("m_str_16_strtok_r", "NULL inputs", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strsep                                                             */
static snap_t m_str_17_strsep(void) {
    snap_t s; cb_t c; char *p; char *t;
    snap_init(&s);

    SC("normal tokenization");
    cb_set(&c, "a,b,c"); p = c.b; errno = 0;
    t = strsep(&p, ",");
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "a") == 0);
    snap_err(&s, errno);
    t = strsep(&p, ",");
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "b") == 0);
    t = strsep(&p, ",");
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "c") == 0);
    t = strsep(&p, ",");
    snap_ptr(&s, -1); CHK(t == NULL);
    snap_dest(&s, &c);

    SC("no delimiter");
    cb_set(&c, "solo"); p = c.b; errno = 0;
    t = strsep(&p, ",");
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "solo") == 0);
    t = strsep(&p, ",");
    snap_ptr(&s, -1); CHK(t == NULL);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty field");
    cb_set(&c, ",x"); p = c.b; errno = 0;
    t = strsep(&p, ",");
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty string");
    cb_set(&c, ""); p = c.b; errno = 0;
    t = strsep(&p, ",");
    snap_ptr(&s, offof(c.b, t)); CHK(t != NULL && strcmp(t, "") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);
    ms_skip("m_str_17_strsep", "NULL inputs", "undefined - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strspn                                                             */
static snap_t m_str_18_strspn(void) {
    snap_t s; cb_t c; size_t n;
    snap_init(&s);

    SC("normal prefix");
    cb_set(&c, "abcXYZ"); errno = 0; n = strspn(c.b, "abc");
    snap_r(&s, (long)n); CHK(n == 3); snap_err(&s, errno); snap_dest(&s, &c);

    SC("whole string");
    cb_set(&c, "abc"); errno = 0; n = strspn(c.b, "abc");
    snap_r(&s, (long)n); CHK(n == 3); snap_err(&s, errno);

    SC("no match");
    cb_set(&c, "xyz"); errno = 0; n = strspn(c.b, "abc");
    snap_r(&s, (long)n); CHK(n == 0); snap_err(&s, errno);

    SC("empty accept set");
    cb_set(&c, "abc"); errno = 0; n = strspn(c.b, "");
    snap_r(&s, (long)n); CHK(n == 0); snap_err(&s, errno);

    SC("empty string");
    cb_set(&c, ""); errno = 0; n = strspn(c.b, "abc");
    snap_r(&s, (long)n); CHK(n == 0); snap_err(&s, errno);
    ms_skip("m_str_18_strspn", "NULL", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strcspn                                                            */
static snap_t m_str_19_strcspn(void) {
    snap_t s; cb_t c; size_t n;
    snap_init(&s);

    SC("normal prefix");
    cb_set(&c, "abcXYZ"); errno = 0; n = strcspn(c.b, "XYZ");
    snap_r(&s, (long)n); CHK(n == 3); snap_err(&s, errno); snap_dest(&s, &c);

    SC("whole string");
    cb_set(&c, "abc"); errno = 0; n = strcspn(c.b, "XYZ");
    snap_r(&s, (long)n); CHK(n == 3); snap_err(&s, errno);

    SC("match at start");
    cb_set(&c, "xyz"); errno = 0; n = strcspn(c.b, "xyz");
    snap_r(&s, (long)n); CHK(n == 0); snap_err(&s, errno);

    SC("empty reject set");
    cb_set(&c, "abc"); errno = 0; n = strcspn(c.b, "");
    snap_r(&s, (long)n); CHK(n == 3); snap_err(&s, errno);

    SC("empty string");
    cb_set(&c, ""); errno = 0; n = strcspn(c.b, "abc");
    snap_r(&s, (long)n); CHK(n == 0); snap_err(&s, errno);
    ms_skip("m_str_19_strcspn", "NULL", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: strpbrk                                                            */
static snap_t m_str_20_strpbrk(void) {
    snap_t s; cb_t c; char *p;
    snap_init(&s);

    SC("found");
    cb_set(&c, "hello world"); errno = 0; p = strpbrk(c.b, "wo");
    snap_ptr(&s, offof(c.b, p)); CHK(p == c.b + 4); snap_err(&s, errno); snap_dest(&s, &c);

    SC("not found");
    cb_set(&c, "hello"); errno = 0; p = strpbrk(c.b, "xyz");
    snap_ptr(&s, -1); CHK(p == NULL); snap_err(&s, errno);

    SC("empty charset");
    cb_set(&c, "hello"); errno = 0; p = strpbrk(c.b, "");
    snap_ptr(&s, -1); CHK(p == NULL); snap_err(&s, errno);

    SC("match at start");
    cb_set(&c, "abc"); errno = 0; p = strpbrk(c.b, "a");
    snap_ptr(&s, offof(c.b, p)); CHK(p == c.b); snap_err(&s, errno);

    SC("empty string");
    cb_set(&c, ""); errno = 0; p = strpbrk(c.b, "a");
    snap_ptr(&s, -1); CHK(p == NULL); snap_err(&s, errno);
    ms_skip("m_str_20_strpbrk", "NULL", "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: stpcpy                                                             */
static snap_t m_str_21_stpcpy(void) {
    snap_t s; cb_t c; char *e;
    snap_init(&s);

    SC("normal");
    cb_init(&c, 0); errno = 0; e = stpcpy(c.b, "abc");
    snap_ptr(&s, offof(c.b, e)); CHK(e == c.b + 3); CHK(*e == 0);
    CHK(strcmp(c.b, "abc") == 0); snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty string");
    cb_init(&c, 'X'); errno = 0; e = stpcpy(c.b, "");
    snap_ptr(&s, offof(c.b, e)); CHK(e == c.b); CHK(c.b[0] == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("exact capacity");
    cb_init(&c, 0); errno = 0; e = stpcpy(c.b, "abcdef");
    snap_ptr(&s, offof(c.b, e)); CHK(e == c.b + 6); CHK(*e == 0);
    snap_err(&s, errno); snap_dest(&s, &c);
    ms_skip("m_str_21_stpcpy", "NULL / no-bound overflow",
            "undefined by ISO C - not provoked");
    return s;
}

/* ========================================================================= */
/* METHOD: stpncpy                                                            */
static snap_t m_str_22_stpncpy(void) {
    snap_t s; cb_t c; char *e;
    snap_init(&s);

    SC("normal n within source");
    cb_init(&c, 0); errno = 0; e = stpncpy(c.b, "abcdef", 3);
    snap_ptr(&s, offof(c.b, e)); CHK(e == c.b + 3); CHK(memcmp(c.b, "abc", 3) == 0);
    CHK(*e == 0); snap_err(&s, errno); snap_dest(&s, &c);

    SC("zero length");
    cb_set(&c, "xyz"); errno = 0; e = stpncpy(c.b, "abc", 0);
    snap_ptr(&s, offof(c.b, e)); CHK(e == c.b); CHK(strcmp(c.b, "xyz") == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("n greater than source length pads with NUL");
    cb_init(&c, 0); errno = 0; e = stpncpy(c.b, "ab", 5);
    snap_ptr(&s, offof(c.b, e)); CHK(memcmp(c.b, "ab", 2) == 0);
    CHK(c.b[2] == 0 && c.b[3] == 0 && c.b[4] == 0);
    snap_err(&s, errno); snap_dest(&s, &c);

    SC("empty source");
    cb_init(&c, 'X'); errno = 0; e = stpncpy(c.b, "", 4);
    snap_ptr(&s, offof(c.b, e)); CHK(e == c.b); CHK(c.b[0] == 0);
    snap_err(&s, errno); snap_dest(&s, &c);
    ms_skip("m_str_22_stpncpy", "NULL", "undefined by ISO C - not provoked");
    return s;
}

/* -------------------------------------------------------------------- main */
int main(void) {
    (void)g_skips; /* reported via SKIP lines; kept as a diagnostic counter */
#ifndef PLAIN_LIBC
    /* Tests that inspect NULL / ERANGE / constraint return values must not be
     * terminated by the SafeCRT unguarded-violation trap. */
    cplus_runtime_set_violation_trap(0);
#endif
    run_method("m_str_01_strcpy",   m_str_01_strcpy);
    run_method("m_str_02_strncpy",  m_str_02_strncpy);
    run_method("m_str_03_strcat",   m_str_03_strcat);
    run_method("m_str_04_strncat",  m_str_04_strncat);
    run_method("m_str_05_strlen",   m_str_05_strlen);
    run_method("m_str_06_strnlen",  m_str_06_strnlen);
    run_method("m_str_07_strcmp",   m_str_07_strcmp);
    run_method("m_str_08_strncmp",  m_str_08_strncmp);
    run_method("m_str_09_strchr",   m_str_09_strchr);
    run_method("m_str_10_strrchr",  m_str_10_strrchr);
    run_method("m_str_11_strstr",   m_str_11_strstr);
    run_method("m_str_12_strcasestr", m_str_12_strcasestr);
    run_method("m_str_13_strdup",   m_str_13_strdup);
    run_method("m_str_14_strndup",  m_str_14_strndup);
    run_method("m_str_15_strtok",   m_str_15_strtok);
    run_method("m_str_16_strtok_r", m_str_16_strtok_r);
    run_method("m_str_17_strsep",   m_str_17_strsep);
    run_method("m_str_18_strspn",   m_str_18_strspn);
    run_method("m_str_19_strcspn",  m_str_19_strcspn);
    run_method("m_str_20_strpbrk",  m_str_20_strpbrk);
    run_method("m_str_21_stpcpy",   m_str_21_stpcpy);
    run_method("m_str_22_stpncpy",  m_str_22_stpncpy);

    ms_puts("methods_string: ");
    ms_num(g_checks - g_fails);
    ms_puts("/"); ms_num(g_checks); ms_puts(" checks ok\n");
    return 0;
}
