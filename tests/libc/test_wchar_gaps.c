/* Tests for the ISO C <wchar.h> API gaps closed from ISO_C_FULL_INVENTORY.
 *
 * Every cplus_* entry point is compared against the identically named host
 * function on valid wide strings under the "C" locale (host differential),
 * plus boundary/zero cases. NULL is exercised only where ISO C defines the
 * behaviour (mbsinit); the other functions require valid pointers in raw C
 * and NULL on them is UB, so they are deliberately not invoked that way.
 * Logging ON/OFF parity proves that emitting entry markers never changes a
 * wrapper's observable result.
 */
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <locale.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

/* Local declarations (matching tests/libc/test_wchar_multibyte.c style). */
extern int cplus_wcscoll(const wchar_t *, const wchar_t *);
extern size_t cplus_wcsxfrm(wchar_t *, const wchar_t *, size_t);
extern wchar_t *cplus_wcschr(const wchar_t *, wchar_t);
extern wchar_t *cplus_wcsrchr(const wchar_t *, wchar_t);
extern wchar_t *cplus_wcspbrk(const wchar_t *, const wchar_t *);
extern size_t cplus_wcsspn(const wchar_t *, const wchar_t *);
extern size_t cplus_wcscspn(const wchar_t *, const wchar_t *);
extern wchar_t *cplus_wmemchr(const wchar_t *, wchar_t, size_t);
extern wchar_t *cplus_fgetws(wchar_t *, int, FILE *);
extern int cplus_fputws(const wchar_t *, FILE *); /* ISO C: returns int */
extern int cplus_mbsinit(const mbstate_t *);
extern void cplus_libc_log_set_enabled(int);

#define LOG_PREFIX "[CPLUS-LIBC] "

static int sgn(int x) { return (x > 0) - (x < 0); }

static ptrdiff_t off(const wchar_t *base, const wchar_t *p) {
    return p ? (ptrdiff_t)(p - base) : (ptrdiff_t)-1;
}

/* ------------------------------------------------------------------ */
/* Host differential on valid wide strings                            */
/* ------------------------------------------------------------------ */
static void test_search(void) {
    const wchar_t s[] = L"abcaXbca";
    const wchar_t set[] = L"cX";

    /* wcschr: present, absent, and the terminating L'\0'. */
    assert(off(s, cplus_wcschr(s, L'c')) == off(s, wcschr(s, L'c')));
    assert(off(s, cplus_wcschr(s, L'z')) == off(s, wcschr(s, L'z')));
    assert(off(s, cplus_wcschr(s, L'\0')) == off(s, wcschr(s, L'\0')));

    /* wcsrchr: last occurrence differs from first for repeated 'c'. */
    assert(off(s, cplus_wcsrchr(s, L'c')) == off(s, wcsrchr(s, L'c')));
    assert(off(s, cplus_wcsrchr(s, L'z')) == off(s, wcsrchr(s, L'z')));
    assert(off(s, cplus_wcsrchr(s, L'\0')) == off(s, wcsrchr(s, L'\0')));

    /* wcspbrk: first member of a set, and no match. */
    assert(off(s, cplus_wcspbrk(s, set)) == off(s, wcspbrk(s, set)));
    assert(off(s, cplus_wcspbrk(s, L"Q!")) == off(s, wcspbrk(s, L"Q!")));

    /* wcsspn / wcscspn: prefix and its complement. */
    assert(cplus_wcsspn(L"abcXYZ", L"abc") == wcsspn(L"abcXYZ", L"abc"));
    assert(cplus_wcscspn(L"abcXYZ", L"abc") == wcscspn(L"abcXYZ", L"abc"));

    /* Boundary / zero cases: empty string and empty set. */
    assert(cplus_wcsspn(L"", L"abc") == wcsspn(L"", L"abc"));
    assert(cplus_wcsspn(L"abc", L"") == wcsspn(L"abc", L""));
    assert(cplus_wcscspn(L"", L"abc") == wcscspn(L"", L"abc"));
    assert(cplus_wcscspn(L"abc", L"") == wcscspn(L"abc", L""));
    assert(cplus_wcsspn(L"aaa", L"a") == 3);
    assert(cplus_wcscspn(L"aaa", L"a") == 0);

    /* wmemchr: within n, beyond n, and the n == 0 case (valid pointer). */
    assert(off(s, cplus_wmemchr(s, L'X', 8)) == off(s, wmemchr(s, L'X', 8)));
    assert(cplus_wmemchr(s, L'z', 8) == wmemchr(s, L'z', 8)); /* both NULL */
    assert(cplus_wmemchr(s, L'a', 0) == wmemchr(s, L'a', 0)); /* both NULL */
    assert(cplus_wmemchr(s, L'a', 1) == wmemchr(s, L'a', 1));
}

static void test_collation(void) {
    static const wchar_t *pairs[][2] = {
        {L"", L""},       {L"abc", L"abc"}, {L"abc", L"abd"},
        {L"abd", L"abc"}, {L"a", L"aa"},    {L"A", L"a"},
        {L"007", L"7"}
    };
    size_t i;
    for (i = 0; i < sizeof pairs / sizeof pairs[0]; ++i) {
        assert(sgn(cplus_wcscoll(pairs[i][0], pairs[i][1])) ==
               sgn(wcscoll(pairs[i][0], pairs[i][1])));
    }

    /* wcsxfrm: length query followed by a full transform. */
    {
        const wchar_t src[] = L"collate-me";
        wchar_t a[64], b[64], z[8];
        size_t na, nb;
        memset(a, 0, sizeof a);
        memset(b, 0, sizeof b);
        na = cplus_wcsxfrm(a, src, 0);
        nb = wcsxfrm(b, src, 0);
        assert(na == nb && na < sizeof a / sizeof a[0]);

        memset(a, 0, sizeof a);
        memset(b, 0, sizeof b);
        assert(cplus_wcsxfrm(a, src, na + 1) == wcsxfrm(b, src, nb + 1));
        assert(wcscmp(a, b) == 0);

        /* n == 0 on a valid buffer: reports length, writes nothing. */
        wcscpy(z, L"keep");
        assert(cplus_wcsxfrm(z, L"abc", 0) == wcsxfrm(z, L"abc", 0));
        assert(wcscmp(z, L"keep") == 0);
    }
}

static void test_mbsinit(void) {
    mbstate_t st;
    memset(&st, 0, sizeof st);
    assert((cplus_mbsinit(&st) != 0) == (mbsinit(&st) != 0));
    assert(cplus_mbsinit(&st) != 0);

    /* ISO C defines NULL here: it reports an initial state (nonzero). */
    assert(cplus_mbsinit(NULL) == mbsinit(NULL));
    assert(cplus_mbsinit(NULL) != 0);
    /* Note: a non-initial mbstate_t cannot be produced in the "C" locale;
     * differential coverage is limited to the initial state and NULL. */
}

static void test_streams(void) {
    FILE *a = tmpfile(), *b = tmpfile();
    wchar_t wa[32], wb[32];
    const wchar_t line[] = L"wide line\n";
    int ra, rb;
    assert(a != NULL && b != NULL);

    /* fputws: ISO C int return, nonnegative on success. */
    ra = cplus_fputws(line, a);
    rb = fputws(line, b);
    assert(ra >= 0 && rb >= 0);
    assert(sgn(ra) == sgn(rb));

    /* fgetws: wchar_t* return, identical text via wrapper and host. */
    rewind(a);
    rewind(b);
    memset(wa, 0, sizeof wa);
    memset(wb, 0, sizeof wb);
    assert(cplus_fgetws(wa, 32, a) == wa);
    assert(fgetws(wb, 32, b) == wb);
    assert(wcscmp(wa, wb) == 0);
    assert(wcscmp(wa, line) == 0);

    /* EOF boundary: no characters read -> NULL from both. */
    assert(cplus_fgetws(wa, 32, a) == NULL);
    assert(fgetws(wb, 32, b) == NULL);

    /* n == 1 boundary: the result is host-defined (e.g. macOS returns NULL
     * and leaves the buffer unchanged, while glibc writes only L'\0'). The
     * wrapper delegates, so compare it against the host instead of assuming
     * a fixed outcome. */
    rewind(a);
    rewind(b);
    memset(wa, 0x7f, sizeof wa);
    memset(wb, 0x7f, sizeof wb);
    {
        wchar_t *ra = cplus_fgetws(wa, 1, a);
        wchar_t *rb = fgetws(wb, 1, b);
        assert((ra == wa) == (rb == wb));
        assert((ra == NULL) == (rb == NULL));
        assert(memcmp(wa, wb, sizeof wa) == 0);
    }

    fclose(a);
    fclose(b);
}

/* ------------------------------------------------------------------ */
/* Logging ON/OFF parity                                              */
/* ------------------------------------------------------------------ */
#ifndef _WIN32
typedef struct { int saved, rd, wr, active; } Cap;

static int cap_start(Cap *c) {
    int fds[2];
    c->saved = c->rd = c->wr = -1;
    c->active = 0;
    if (pipe(fds) != 0) return -1;
    c->rd = fds[0];
    c->wr = fds[1];
    c->saved = dup(STDERR_FILENO);
    if (c->saved < 0) {
        close(c->rd); close(c->wr);
        c->rd = c->wr = -1;
        return -1;
    }
    if (dup2(c->wr, STDERR_FILENO) < 0) {
        dup2(c->saved, STDERR_FILENO);
        close(c->saved); close(c->rd); close(c->wr);
        c->saved = c->rd = c->wr = -1;
        return -1;
    }
    {
        int fl = fcntl(c->rd, F_GETFL, 0);
        if (fl >= 0) (void)fcntl(c->rd, F_SETFL, fl | O_NONBLOCK);
    }
    c->active = 1;
    return 0;
}

static size_t cap_finish(Cap *c, char *buf, size_t cap) {
    size_t n = 0;
    if (c->active) {
        (void)fflush(stderr);
        (void)dup2(c->saved, STDERR_FILENO); /* restore before any assert */
        close(c->saved);
        close(c->wr); /* last write end closed => EOF after drain */
        c->saved = -1;
        c->wr = -1;
        c->active = 0;
    }
    if (cap == 0) {
        if (c->rd >= 0) { close(c->rd); c->rd = -1; }
        return 0;
    }
    for (;;) {
        ssize_t k;
        if (n >= cap - 1) break; /* hard cap */
        k = read(c->rd, buf + n, cap - 1 - n);
        if (k > 0) { n += (size_t)k; continue; }
        if (k == 0) break;                 /* EOF */
        if (errno == EINTR) continue;
        break;                             /* EAGAIN or error: stop */
    }
    buf[n] = '\0';
    if (c->rd >= 0) { close(c->rd); c->rd = -1; }
    return n;
}
#endif /* !_WIN32 */

typedef struct {
    int coll_sign;
    size_t xfrm_len;
    wchar_t xfrm[64];
    ptrdiff_t chr, rchr, pbrk;
    size_t spn, cspn;
    ptrdiff_t memchr_off;
    int fgetws_ok;
    wchar_t fgetws_buf[32];
    int fputws_sign;
    int mbsinit_zero;
    int mbsinit_null;
} Results;

static void fill(Results *r) {
    static const wchar_t s[] = L"abcaXbca";
    static const wchar_t set[] = L"cX";
    wchar_t scratch[64];
    mbstate_t st;
    FILE *fp;

    memset(r, 0, sizeof *r);
    memset(scratch, 0, sizeof scratch);
    r->coll_sign = sgn(cplus_wcscoll(L"abc", L"abd"));
    r->xfrm_len = cplus_wcsxfrm(scratch, L"collate-me", 0);
    (void)cplus_wcsxfrm(r->xfrm, L"collate-me", r->xfrm_len + 1);
    r->chr = off(s, cplus_wcschr(s, L'c'));
    r->rchr = off(s, cplus_wcsrchr(s, L'c'));
    r->pbrk = off(s, cplus_wcspbrk(s, set));
    r->spn = cplus_wcsspn(L"abcXYZ", L"abc");
    r->cspn = cplus_wcscspn(L"abcXYZ", L"abc");
    r->memchr_off = off(s, cplus_wmemchr(s, L'X', 8));

    fp = tmpfile();
    assert(fp != NULL);
    r->fputws_sign = sgn(cplus_fputws(L"wide\n", fp));
    rewind(fp);
    memset(r->fgetws_buf, 0, sizeof r->fgetws_buf);
    r->fgetws_ok = (cplus_fgetws(r->fgetws_buf, 32, fp) == r->fgetws_buf);
    fclose(fp);

    memset(&st, 0, sizeof st);
    r->mbsinit_zero = (cplus_mbsinit(&st) != 0);
    r->mbsinit_null = (cplus_mbsinit(NULL) != 0);
}

static void test_logging_parity(void) {
    Results on, offv;
#ifndef _WIN32
    static const char *markers[] = {
        "wcscoll", "wcsxfrm", "wcschr", "wcsrchr", "wcspbrk",
        "wcsspn", "wcscspn", "wmemchr", "fgetws", "fputws", "mbsinit"
    };
    char buf[8192];
    Cap c;
    size_t i, n;
#endif

    /* Logging OFF: no markers, results captured. */
    cplus_libc_log_set_enabled(0);
#ifndef _WIN32
    if (cap_start(&c) == 0) {
        fill(&offv);
        n = cap_finish(&c, buf, sizeof buf);
        assert(n < sizeof buf);
        assert(strstr(buf, LOG_PREFIX) == NULL);
    } else
#endif
        fill(&offv);

    /* Logging ON: every wrapper emits its entry marker; results unchanged. */
    cplus_libc_log_set_enabled(1);
#ifndef _WIN32
    if (cap_start(&c) == 0) {
        fill(&on);
        n = cap_finish(&c, buf, sizeof buf);
        assert(n < sizeof buf);
        for (i = 0; i < sizeof markers / sizeof markers[0]; ++i) {
            char needle[64];
            (void)snprintf(needle, sizeof needle,
                           LOG_PREFIX "%s: enter", markers[i]);
            assert(strstr(buf, needle) != NULL);
        }
    } else
#endif
        fill(&on);
    cplus_libc_log_set_enabled(0);

    assert(memcmp(&on, &offv, sizeof on) == 0);
}

int main(void) {
    assert(setlocale(LC_ALL, "C") != NULL);

    test_search();
    test_collation();
    test_mbsinit();
    test_streams();
    test_logging_parity();

    puts("wchar_gaps: ok");
    return 0;
}
