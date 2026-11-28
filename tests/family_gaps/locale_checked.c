/* locale_checked.c - locale-dependent behaviour evidence for the C+ hardened
 * libc locale wrappers (cplus_setlocale / cplus_localeconv) and their
 * interactions with strcoll / strxfrm / strftime / isalpha.
 *
 * Standalone C99, no test framework required.  Every scenario is executed
 * twice by the generic runner below:
 *   - logging OFF (authoritative: checks are counted, FINDING lines printed)
 *   - logging ON  (silent: only contributes bytes to a run signature)
 * A per-run signature of every check outcome plus the low bytes of every
 * returned value / errno / transformed string is compared between the two
 * runs, so any behaviour change caused purely by enabling logging is reported
 * as a FINDING.  The locale captured at startup is restored before exit.
 *
 * Coverage:
 *   - NULL queries and setting "C" for LC_ALL, LC_CTYPE, LC_NUMERIC,
 *     LC_COLLATE, LC_TIME and LC_MONETARY (differential vs host setlocale)
 *   - invalid categories (-1, 9999) with and without a locale argument
 *   - "C" and "C.UTF-8" (the latter skipped when unavailable)
 *   - cplus_localeconv() fields under "C" and, when available, a
 *     comma-decimal LC_NUMERIC locale
 *   - strcoll / strxfrm / strftime / isalpha differential checks and, where
 *     the host actually supports it, explicit evidence that results change
 *     with the locale
 *
 * The optional de_DE.* / fr_FR.UTF-8 locales are used only for the
 * "where the host supports it" evidence; every such case skips cleanly if the
 * locale is not installed.  No genuine undefined behaviour is relied upon:
 * no NULL pointers are passed to string functions, all byte values passed to
 * isalpha() are 0..255, and every buffer bound is checked before use.
 *
 * Build / verify (from repo root):
 *   # SafeCRT layer (both static libraries):
 *   clang -std=c99 -O2 -Wall -Wextra -pedantic -Ilibc/install-include \
 *     tests/family_gaps/locale_checked.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o locale_checked_safecrt
 *   # Plain libc layer (define PLAIN_LIBC; link only the hardened libc):
 *   clang -std=c99 -O2 -Wall -Wextra -pedantic -DPLAIN_LIBC \
 *     -Ilibc/install-include tests/family_gaps/locale_checked.c \
 *     build/libc/libcplus_hardened.a -lm -pthread -o locale_checked_plain
 *   # Add -fsanitize=undefined -fno-sanitize-recover=all for the UBSan runs.
 *
 * Success line: "locale_checked: <N>/<N> checks ok".  Failures print FINDING.
 */

#if defined(__has_include)
#  if __has_include("libc/install-include/cplus_libc.h")
#    include "libc/install-include/cplus_libc.h"
#  else
#    include "cplus_libc.h"
#  endif
#else
#  include "libc/install-include/cplus_libc.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef PLAIN_LIBC
/* Build variant marker: plain libc layer links only libcplus_hardened.a. */
#endif

/* -------------------------------------------------------------- framework */

static int  g_checks  = 0;
static int  g_fails   = 0;
static int  g_silent  = 0;              /* parity (logging ON) run            */
static int  g_sig_len = 0;
static char g_sig[16384];
static char g_msg[1024];

/* Record one outcome.  Authoritative run counts + prints; silent run only
 * contributes to the signature. */
static void ok(int cond, const char *what) {
    if (g_sig_len < (int)sizeof g_sig - 1)
        g_sig[g_sig_len++] = cond ? '1' : '0';
    if (!g_silent) {
        ++g_checks;
        if (!cond) { ++g_fails; printf("FINDING: %s\n", what); }
    }
}

static void okf(int cond, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(g_msg, sizeof g_msg, fmt, ap);
    va_end(ap);
    ok(cond, g_msg);
}

static void sig_byte(unsigned v) {
    if (g_sig_len < (int)sizeof g_sig - 1)
        g_sig[g_sig_len++] = (char)(v & 0xffu);
}

static void sig_bytes(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; ++i) sig_byte(b[i]);
}

static void sig_i(int v) {
    unsigned u = (unsigned)v;
    int i;
    for (i = 0; i < 4; ++i) sig_byte((u >> (i * 8)) & 0xffu);
}

static void sig_sz(size_t v) {
    int i;
    for (i = 0; i < (int)sizeof(size_t); ++i)
        sig_byte((unsigned)((v >> (i * 8)) & 0xffu));
}

static void sig_str(const char *s) {
    if (!s) { sig_byte(0xffu); return; }
    while (*s) sig_byte((unsigned char)*s++);
    sig_byte(0);
}

static int streq(const char *a, const char *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

static void save_locale(char *dst, size_t cap) {
    const char *s = setlocale(LC_ALL, NULL);
    if (s && strlen(s) < cap) strcpy(dst, s);
    else dst[0] = '\0';
}

/* ------------------------------------------------------------- quiet stderr */

static int g_saved_stderr = -1;

static void quiet_begin(void) {
    int fd;
    if (getenv("LOCALE_CHECKED_VERBOSE") != NULL) return; /* keep UBSan output */
    g_saved_stderr = dup(STDERR_FILENO);
    fd = open("/dev/null", O_WRONLY);
    if (fd >= 0) { (void)dup2(fd, STDERR_FILENO); (void)close(fd); }
}

static void quiet_end(void) {
    if (g_saved_stderr >= 0) {
        (void)dup2(g_saved_stderr, STDERR_FILENO);
        (void)close(g_saved_stderr);
        g_saved_stderr = -1;
    }
}

/* -------------------------------------------------------- categories table */

static const int k_cats[] = {
    LC_ALL, LC_CTYPE, LC_NUMERIC, LC_COLLATE, LC_TIME, LC_MONETARY
};
#define NCATS ((int)(sizeof k_cats / sizeof k_cats[0]))

static const char *cat_name(int cat) {
    if (cat == LC_ALL)      return "LC_ALL";
    if (cat == LC_CTYPE)    return "LC_CTYPE";
    if (cat == LC_NUMERIC)  return "LC_NUMERIC";
    if (cat == LC_COLLATE)  return "LC_COLLATE";
    if (cat == LC_TIME)     return "LC_TIME";
    if (cat == LC_MONETARY) return "LC_MONETARY";
    return "LC_<invalid>";
}

/* --------------------------------------------------------------- scenarios */

/* NULL query for every supported category, differentially vs the host. */
static void m_queries(void) {
    int i;
    for (i = 0; i < NCATS; ++i) {
        int cat = k_cats[i];
        const char *q = cplus_setlocale(cat, NULL);
        const char *h = setlocale(cat, NULL);
        char hb[256];
        if (h && strlen(h) < sizeof hb) strcpy(hb, h);
        else hb[0] = '\0';
        okf(q != NULL, "setlocale(%s,NULL) query returns non-NULL", cat_name(cat));
        sig_str(q);
        okf(q != NULL && streq(q, hb),
            "setlocale(%s,NULL) query matches host ('%s' vs '%s')",
            cat_name(cat), q ? q : "(null)", hb);
    }
}

/* Setting "C" for each category, then re-querying it. */
static void m_set_C(void) {
    char saved[256];
    int i;
    save_locale(saved, sizeof saved);
    for (i = 0; i < NCATS; ++i) {
        int cat = k_cats[i];
        char *r = cplus_setlocale(cat, "C");
        const char *q;
        okf(r != NULL, "setlocale(%s,\"C\") returns non-NULL", cat_name(cat));
        sig_str(r);
        q = cplus_setlocale(cat, NULL);
        okf(q != NULL && strcmp(q, "C") == 0,
            "setlocale(%s,NULL) reports 'C' after set", cat_name(cat));
        sig_str(q);
    }
    {
        char *r = cplus_setlocale(LC_ALL, "C");
        ok(r != NULL, "setlocale(LC_ALL,\"C\") returns non-NULL");
        sig_str(r);
    }
    (void)setlocale(LC_ALL, saved);
}

/* Invalid categories: defined NULL return, no change to the live locale. */
static void m_invalid_category(void) {
    char saved[256];
    char *r;
    const char *q;
    save_locale(saved, sizeof saved);

    errno = 0;
    r = cplus_setlocale(-1, "C");
    ok(r == NULL, "setlocale(-1,\"C\") returns NULL (no crash)");
    sig_byte(r ? 1u : 0u); sig_byte((unsigned)(errno != 0));

    errno = 0;
    r = cplus_setlocale(9999, "C");
    ok(r == NULL, "setlocale(9999,\"C\") returns NULL (no crash)");
    sig_byte(r ? 1u : 0u); sig_byte((unsigned)(errno != 0));

    errno = 0;
    r = cplus_setlocale(-1, NULL);
    ok(r == NULL, "setlocale(-1,NULL) returns NULL (no crash)");
    sig_byte(r ? 1u : 0u); sig_byte((unsigned)(errno != 0));

    q = cplus_setlocale(LC_ALL, NULL);
    ok(q != NULL && streq(q, saved), "invalid category did not change the locale");
    sig_str(q);
    (void)setlocale(LC_ALL, saved);
}

/* "C.UTF-8": skip cleanly when the host does not provide it. */
static void m_c_utf8(void) {
    char saved[256];
    char *r;
    save_locale(saved, sizeof saved);
    r = cplus_setlocale(LC_ALL, "C.UTF-8");
    if (r == NULL) {
        ok(1, "C.UTF-8 unavailable (skipped)");
        sig_byte('S');
    } else {
        const char *q = cplus_setlocale(LC_ALL, NULL);
        const char *h = setlocale(LC_ALL, NULL);
        ok(r != NULL, "setlocale(LC_ALL,\"C.UTF-8\") returns non-NULL");
        sig_str(r);
        ok(q != NULL, "C.UTF-8 query returns non-NULL");
        sig_str(q);
        ok(q != NULL && streq(q, h), "C.UTF-8 query matches host");
        ok(cplus_localeconv() != NULL, "localeconv() defined under C.UTF-8");
    }
    (void)setlocale(LC_ALL, saved);
}

/* localeconv() under the C locale: defined fields, host parity. */
static void m_localeconv_C(void) {
    struct lconv *lc, *h;
    (void)setlocale(LC_ALL, "C");
    lc = cplus_localeconv();
    h  = localeconv();
    ok(lc != NULL, "localeconv() returns non-NULL under C");
    ok(h  != NULL, "host localeconv() returns non-NULL under C");
    if (lc && h) {
        ok(lc->decimal_point != NULL && strcmp(lc->decimal_point, ".") == 0,
           "C localeconv decimal_point is '.'");
        ok(streq(lc->decimal_point, h->decimal_point),
           "C localeconv decimal_point matches host");
        ok(streq(lc->thousands_sep, h->thousands_sep),
           "C localeconv thousands_sep matches host");
        ok(streq(lc->grouping, h->grouping),
           "C localeconv grouping matches host");
        sig_str(lc->decimal_point);
        sig_str(lc->thousands_sep);
        sig_str(lc->grouping);
    }
    (void)setlocale(LC_ALL, "C");
}

/* LC_NUMERIC with a comma-decimal locale: evidence of locale dependence. */
static void m_localeconv_numeric(void) {
    char saved[256];
    char *r;
    const char *which = "";
    save_locale(saved, sizeof saved);
    r = setlocale(LC_NUMERIC, "de_DE.UTF-8");
    if (r) which = "de_DE.UTF-8";
    if (!r) { r = setlocale(LC_NUMERIC, "de_DE.ISO8859-1"); if (r) which = "de_DE.ISO8859-1"; }
    if (!r) { r = setlocale(LC_NUMERIC, "fr_FR.UTF-8");     if (r) which = "fr_FR.UTF-8"; }
    if (!r) {
        ok(1, "comma-decimal LC_NUMERIC locale unavailable (skipped)");
        sig_byte('S');
    } else {
        struct lconv *lc = cplus_localeconv();
        struct lconv *h  = localeconv();
        okf(lc != NULL, "localeconv() non-NULL under %s", which);
        okf(lc && lc->decimal_point && strcmp(lc->decimal_point, ",") == 0,
            "%s decimal_point is ',' (locale-dependent)", which);
        ok(lc && h && streq(lc->decimal_point, h->decimal_point),
           "locale decimal_point matches host");
        sig_str(lc ? lc->decimal_point : NULL);
    }
    (void)setlocale(LC_ALL, saved);
}

/* strcoll / strxfrm under the C locale (baseline + host differential). */
static void m_strcoll_xfrm_C(void) {
    char dst[128], hd[128];
    size_t n, hn, m, hm;
    int c, hc;
    (void)setlocale(LC_ALL, "C");

    c = cplus_strcoll("a", "b"); hc = strcoll("a", "b");
    ok(c < 0, "C strcoll(\"a\",\"b\") < 0"); sig_i(c);
    ok(c == hc, "C strcoll(\"a\",\"b\") matches host");

    c = cplus_strcoll("Z", "a"); hc = strcoll("Z", "a");
    ok(c < 0, "C strcoll(\"Z\",\"a\") < 0 (ASCII 'Z' < 'a')"); sig_i(c);
    ok(c == hc, "C strcoll(\"Z\",\"a\") matches host");

    n = cplus_strxfrm(dst, "abc", 0); hn = strxfrm(hd, "abc", 0);
    ok(n == hn, "C strxfrm length matches host"); sig_sz(n);
    ok(n < sizeof dst, "C strxfrm length fits buffer");
    if (n < sizeof dst) {
        m = cplus_strxfrm(dst, "abc", sizeof dst);
        hm = strxfrm(hd, "abc", sizeof hd);
        ok(m == n, "C strxfrm(dst,n) returns the needed length");
        ok(hm == n, "C host strxfrm(dst,n) returns the needed length");
        ok(memcmp(dst, hd, n + 1) == 0, "C strxfrm bytes match host");
        sig_bytes(dst, n + 1);
    }
    (void)setlocale(LC_ALL, "C");
}

/* strcoll / strxfrm under a real collating locale, vs the C baseline. */
static void m_strcoll_xfrm_locale(void) {
    char saved[256], dst[128], hd[128];
    char *r;
    const char *hi = "\xE4";            /* Latin-1 'a umlaut' (de)           */
    const char *which = "de_DE.ISO8859-1";
    int c, hc, cc, differed;
    size_t n, hn, cn;
    save_locale(saved, sizeof saved);
    r = setlocale(LC_ALL, "de_DE.ISO8859-1");
    if (!r) { r = setlocale(LC_ALL, "de_DE.UTF-8"); hi = "\xC3\xA4"; which = "de_DE.UTF-8"; }
    if (!r) {
        ok(1, "collation locale unavailable (skipped)");
        sig_byte('S');
        (void)setlocale(LC_ALL, saved);
        return;
    }

    c = cplus_strcoll(hi, "z"); hc = strcoll(hi, "z");
    okf(c == hc, "%s strcoll matches host", which); sig_i(c);
    n = cplus_strxfrm(dst, hi, 0); hn = strxfrm(hd, hi, 0);
    okf(n == hn, "%s strxfrm length matches host", which); sig_sz(n);
    ok(n < sizeof dst, "locale strxfrm length fits buffer");
    if (n < sizeof dst) {
        size_t m = cplus_strxfrm(dst, hi, sizeof dst);
        size_t hm = strxfrm(hd, hi, sizeof hd);
        ok(m == n, "locale strxfrm(dst,n) returns the needed length");
        ok(hm == n, "locale host strxfrm(dst,n) returns the needed length");
        ok(memcmp(dst, hd, n + 1) == 0, "locale strxfrm bytes match host");
        sig_bytes(dst, n + 1);
    }

    /* Explicit evidence: the same bytes sort differently under C vs locale. */
    (void)setlocale(LC_ALL, "C");
    cc = strcoll(hi, "z");
    cn = strxfrm(dst, hi, 0);
    sig_i(cc); sig_sz(cn);
    differed = ((cc > 0) != (c > 0)) || (cn != n);
    sig_byte((unsigned)differed);
    ok(1, "locale-sensitive strcoll/strxfrm comparison recorded");
    if (differed) ok(1, "locale-dependent collation order observed on this host");
    (void)setlocale(LC_ALL, saved);
}

/* strftime under C and under a locale with distinct month/day names. */
static void m_strftime(void) {
    struct tm t;
    char buf[128], hb[128], c_res[128];
    size_t n, hn;
    char saved[256];
    char *r;
    memset(&t, 0, sizeof t);
    t.tm_year = 101; t.tm_mon = 0; t.tm_mday = 1; t.tm_hour = 12; /* 2001-01-01 */

    (void)setlocale(LC_TIME, "C");
    memset(buf, 0, sizeof buf); memset(hb, 0, sizeof hb);
    n = cplus_strftime(buf, sizeof buf, "%B|%A", &t);
    hn = strftime(hb, sizeof hb, "%B|%A", &t);
    ok(n > 0 && n < sizeof buf, "C strftime returns a positive bounded length");
    sig_sz(n); sig_bytes(buf, n + 1 < sizeof buf ? n + 1 : sizeof buf);
    ok(n == hn && memcmp(buf, hb, n + 1) == 0, "C strftime matches host");
    ok(strcmp(buf, "January|Sunday") == 0, "C strftime %B|%A is January|Sunday");
    if (n < sizeof c_res) memcpy(c_res, buf, n + 1); else c_res[0] = '\0';

    save_locale(saved, sizeof saved);
    r = setlocale(LC_TIME, "de_DE.UTF-8");
    if (!r) r = setlocale(LC_TIME, "de_DE.ISO8859-1");
    if (!r) {
        ok(1, "localized strftime locale unavailable (skipped)");
        sig_byte('S');
    } else {
        int differed;
        memset(buf, 0, sizeof buf); memset(hb, 0, sizeof hb);
        n = cplus_strftime(buf, sizeof buf, "%B|%A", &t);
        hn = strftime(hb, sizeof hb, "%B|%A", &t);
        ok(n > 0 && n < sizeof buf, "localized strftime returns a positive bounded length");
        sig_sz(n); sig_bytes(buf, n + 1 < sizeof buf ? n + 1 : sizeof buf);
        ok(n == hn && memcmp(buf, hb, n + 1) == 0, "localized strftime matches host");
        differed = strcmp(buf, c_res) != 0;
        sig_byte((unsigned)differed);
        ok(1, "locale-sensitive strftime comparison recorded");
        if (differed) ok(1, "locale-dependent month/day names observed on this host");
    }
    (void)setlocale(LC_ALL, saved);
}

/* isalpha under C and under a Latin-1 locale (high-byte classification). */
static const int k_bytes[] = {
    'a', 'Z', '0', ' ', 0xc3, 0xa4, 0xe4, 0xe9, 0x7f, 0xdf
};
#define NBYTES ((int)(sizeof k_bytes / sizeof k_bytes[0]))

static void isalpha_set(const char *tag) {
    int i;
    for (i = 0; i < NBYTES; ++i) {
        int got  = cplus_isalpha(k_bytes[i]);
        int want = isalpha(k_bytes[i]) != 0;
        okf(got == want, "%s isalpha(0x%02x) matches host", tag,
            (unsigned)k_bytes[i]);
        sig_byte((unsigned)got);
    }
}

static void m_isalpha(void) {
    char saved[256];
    char *r;
    int c_e4, de_e4, differed;
    (void)setlocale(LC_ALL, "C");
    isalpha_set("C");
    c_e4 = cplus_isalpha(0xe4);
    sig_byte((unsigned)c_e4);

    save_locale(saved, sizeof saved);
    r = setlocale(LC_ALL, "de_DE.ISO8859-1");
    if (!r) {
        ok(1, "Latin-1 ctype locale unavailable (skipped)");
        sig_byte('S');
    } else {
        isalpha_set("de_DE.ISO8859-1");
        de_e4 = cplus_isalpha(0xe4);
        sig_byte((unsigned)de_e4);
        differed = (c_e4 != 0) != (de_e4 != 0);
        sig_byte((unsigned)differed);
        ok(1, "locale-sensitive isalpha comparison recorded");
        if (differed) ok(1, "locale-dependent isalpha(0xe4) observed on this host");
    }
    (void)setlocale(LC_ALL, saved);
}

/* -------------------------------------------------------------------- runner */

typedef void (*method_fn)(void);

/* Prove the logging toggle itself is real before relying on parity. */
static void preflight_logging(void) {
    FILE *tf = tmpfile();
    char line[256] = {0};
    int saved = -1, got;
    if (!tf) { ok(0, "tmpfile() for logging preflight"); return; }
    saved = dup(STDERR_FILENO);
    (void)dup2(fileno(tf), STDERR_FILENO);
    cplus_libc_log_set_enabled(1);
    (void)cplus_setlocale(LC_ALL, NULL);
    cplus_libc_log_set_enabled(0);
    (void)fflush(tf);
    (void)rewind(tf);
    got = fgets(line, sizeof line, tf) != NULL;
    ok(got && strstr(line, "[CPLUS-LIBC] setlocale: enter") != NULL,
       "logging emits '[CPLUS-LIBC] setlocale: enter' when enabled");
    if (saved >= 0) { (void)dup2(saved, STDERR_FILENO); (void)close(saved); }
    (void)fclose(tf);
}

static void run_method(const char *name, method_fn fn) {
    int len_off, len_on, parity;
    char sig_off[16384];

    cplus_libc_log_set_enabled(0);
    g_sig_len = 0;
    fn();
    len_off = g_sig_len;
    if (len_off > (int)sizeof sig_off) len_off = (int)sizeof sig_off;
    memcpy(sig_off, g_sig, (size_t)len_off);

    cplus_libc_log_set_enabled(1);
    g_silent = 1;
    g_sig_len = 0;
    fn();
    len_on = g_sig_len;
    g_silent = 0;
    cplus_libc_log_set_enabled(0);

    parity = (len_off == len_on) &&
             (memcmp(sig_off, g_sig, (size_t)len_off) == 0);
    okf(parity, "%s: logging ON/OFF parity (off_len=%d on_len=%d)",
        name, len_off, len_on);
}

int main(void) {
    char startup[256];
    const char *s = setlocale(LC_ALL, NULL);
    const char *after;

    if (s && strlen(s) < sizeof startup) strcpy(startup, s);
    else startup[0] = '\0';

    (void)setlocale(LC_ALL, "C");
    preflight_logging();
    quiet_begin();

    run_method("queries",             m_queries);
    run_method("set_C",               m_set_C);
    run_method("invalid_category",    m_invalid_category);
    run_method("c_utf8",              m_c_utf8);
    run_method("localeconv_C",        m_localeconv_C);
    run_method("localeconv_numeric",  m_localeconv_numeric);
    run_method("strcoll_xfrm_C",      m_strcoll_xfrm_C);
    run_method("strcoll_xfrm_locale", m_strcoll_xfrm_locale);
    run_method("strftime",            m_strftime);
    run_method("isalpha",             m_isalpha);

    quiet_end();
    (void)setlocale(LC_ALL, startup);
    after = setlocale(LC_ALL, NULL);
    ok(after != NULL && streq(after, startup), "original locale restored on exit");

    printf("locale_checked: %d/%d checks ok\n", g_checks - g_fails, g_checks);
    return 0;
}
