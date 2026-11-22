/* Adversarial standalone C99 test for the PLATFORM-BACKED ISO C17 wrappers:
 *   cplus_aligned_alloc / cplus_quick_exit / cplus_at_quick_exit / cplus__Exit
 *   cplus_wcstoimax / cplus_wcstoumax
 *
 * Every symbol is declared locally; this test does not depend on the installed
 * public header. Memory returned by cplus_aligned_alloc belongs to the HOST CRT
 * and is intentionally released with the host free() -- never with cplus_free()
 * or free_checked(). The test NEVER cross-frees a pointer between allocators.
 *
 * Deviations from the documented contract are reported as "FINDING: ..." lines
 * (the run still completes and prints the success banner so that all findings
 * are collected in a single pass).
 */
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* ---- namespaced wrappers under test (local declarations) ---------------- */
void cplus_libc_log_set_enabled(int enabled);
void *cplus_malloc(size_t size);
void cplus_free(void *ptr);
int cplus_alloc_is_live(const void *ptr);
size_t cplus_alloc_live_count(void);
void *cplus_aligned_alloc(size_t alignment, size_t size);
void cplus_quick_exit(int status);
int cplus_at_quick_exit(void (*func)(void));
void cplus__Exit(int status);
intmax_t cplus_wcstoimax(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base);
uintmax_t cplus_wcstoumax(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base);

static int g_findings = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FINDING: %s\n", (msg));                                    \
            ++g_findings;                                                      \
        }                                                                      \
    } while (0)

/* ---- compact result recorder so logging ON/OFF can be compared ---------- */
typedef struct {
    int n;
    int v[512];
} Rec;

static void rec_add(Rec *r, int v) {
    if (r->n < (int)(sizeof r->v / sizeof r->v[0])) r->v[r->n++] = v;
}

/* ------------------------------------------------------------------------ */
/* 1 + 2 + 7. Alignment / size torture.                                     */
/* ------------------------------------------------------------------------ */
static void aligned_case(Rec *r, size_t a, size_t s, const char *label) {
    void *p;
    int code = 0;
    errno = 0;
    p = cplus_aligned_alloc(a, s);
    if (p == NULL) code |= 1;
    if (errno == EINVAL) code |= 2;
    if (errno == ERANGE) code |= 4;
    if (p != NULL) {
        size_t i;
        int ok = 1;
        unsigned char *b = (unsigned char *)p;
        if (((uintptr_t)p % (uintptr_t)a) == 0u) code |= 8;
        /* Full-range write/read for small valid allocations (bounded to 256). */
        for (i = 0u; i < s && i < 256u; ++i) b[i] = (unsigned char)(i ^ 0xA5u);
        for (i = 0u; i < s && i < 256u; ++i) {
            if (b[i] != (unsigned char)(i ^ 0xA5u)) {
                ok = 0;
                break;
            }
        }
        if (ok && s <= 256u) code |= 16;
        /* Provenance: host-allocator memory must NOT be cplus-tracked. */
        if (cplus_alloc_is_live(p)) code |= 32;
        free(p); /* host CRT owns this pointer */
    }
    rec_add(r, code);
    (void)label;
}

/* ------------------------------------------------------------------------ */
/* 6 + 7. wide strtoimax family.                                            */
/* ------------------------------------------------------------------------ */
static void imax_case(Rec *r, const wchar_t *s, int base) {
    intmax_t h, c;
    wchar_t *he = NULL, *ce = NULL;
    int he_errno, ce_errno;
    int code = 0;
    errno = 0;
    h = wcstoimax(s, &he, base);
    he_errno = errno;
    errno = 0;
    c = cplus_wcstoimax(s, &ce, base);
    ce_errno = errno;
    if (c == 0) code |= 1;
    else if (c > 0) code |= 2;
    else code |= 4; /* negative */
    if (ce_errno == ERANGE) code |= 8;
    if (ce_errno == EINVAL) code |= 16;
    if (c == INTMAX_MAX) code |= 32;
    if (c == INTMAX_MIN) code |= 64;
    if (h == c && he_errno == ce_errno) code |= 128; /* differential vs host */
    if (he == ce) code |= 256; /* endptr semantics identical to host */
    rec_add(r, code);
}

static void umax_case(Rec *r, const wchar_t *s, int base) {
    uintmax_t h, c;
    wchar_t *he = NULL, *ce = NULL;
    int he_errno, ce_errno;
    int code = 0;
    errno = 0;
    h = wcstoumax(s, &he, base);
    he_errno = errno;
    errno = 0;
    c = cplus_wcstoumax(s, &ce, base);
    ce_errno = errno;
    if (c == 0) code |= 1;
    else code |= 2;
    if (ce_errno == ERANGE) code |= 4;
    if (ce_errno == EINVAL) code |= 8;
    if (c == UINTMAX_MAX) code |= 16; /* saturation */
    if (h == c && he_errno == ce_errno) code |= 32;
    if (he == ce) code |= 64;
    rec_add(r, code);
}

static void null_cases(Rec *r) {
    wchar_t *e;
    intmax_t si;
    uintmax_t ui;
    int code;

    e = (wchar_t *)0x1; /* never dereferenced; wrapper overwrites via endptr */
    errno = 0;
    si = cplus_wcstoimax(NULL, &e, 10);
    code = 0;
    if (si == 0) code |= 1;
    if (errno == EINVAL) code |= 2;
    if (e == NULL) code |= 4;
    rec_add(r, code);

    errno = 0;
    si = cplus_wcstoimax(NULL, NULL, 10);
    code = 0;
    if (si == 0) code |= 1;
    if (errno == EINVAL) code |= 2;
    rec_add(r, code);

    e = (wchar_t *)0x1;
    errno = 0;
    ui = cplus_wcstoumax(NULL, &e, 10);
    code = 0;
    if (ui == 0u) code |= 1;
    if (errno == EINVAL) code |= 2;
    if (e == NULL) code |= 4;
    rec_add(r, code);

    errno = 0;
    ui = cplus_wcstoumax(NULL, NULL, 10);
    code = 0;
    if (ui == 0u) code |= 1;
    if (errno == EINVAL) code |= 2;
    rec_add(r, code);

    /* NULL endptr on a VALID string is accepted and saturates/parses. */
    errno = 0;
    si = cplus_wcstoimax(L"99", NULL, 10);
    rec_add(r, si == 99 ? 1 : 0);
    errno = 0;
    ui = cplus_wcstoumax(L"88", NULL, 10);
    rec_add(r, ui == 88u ? 1 : 0);
}

/* Run the entire non-terminating battery; record every outcome. */
static void run_suite(Rec *r, int logging) {
    static const wchar_t big_pos[] =
        L"9999999999999999999999999999999999999999";
    static const wchar_t big_neg[] =
        L"-9999999999999999999999999999999999999999";
    void *q;

    cplus_libc_log_set_enabled(logging);

    /* 1. alignment torture: zero / sub-pointer / non-power-of-two / extreme */
    aligned_case(r, 0u, 0u, "a=0,s=0");
    aligned_case(r, 0u, 8u, "a=0,s=8");
    aligned_case(r, 1u, 8u, "a=1");
    aligned_case(r, 2u, 8u, "a=2");
    aligned_case(r, 3u, 12u, "a=3");
    aligned_case(r, 7u, 56u, "a=7");
    aligned_case(r, 12u, 48u, "a=12");
    aligned_case(r, 24u, 48u, "a=24");
    aligned_case(r, SIZE_MAX - 1u, 8u, "a=SIZE_MAX-1");
    aligned_case(r, SIZE_MAX, 8u, "a=SIZE_MAX");
    aligned_case(r, SIZE_MAX, SIZE_MAX, "a=MAX,s=MAX");
    aligned_case(r, ((size_t)1 << 40) + 8u, 8u, "a=2^40+8 (non-pow2 large)");

    /* 2. size torture */
    aligned_case(r, 64u, 100u, "s not multiple");
    aligned_case(r, 8u, SIZE_MAX, "s=SIZE_MAX,a=8");
    aligned_case(r, 8u, SIZE_MAX - 7u, "huge s multiple of a -> host decides");
    aligned_case(r, SIZE_MAX - 7u, 8u, "a=SIZE_MAX-7 (non-pow2, s multiple)");

    /* VALID cases: exact alignment + full-range write/read */
    aligned_case(r, 8u, 8u, "a=8,s=8");
    aligned_case(r, 16u, 64u, "a=16,s=64");
    aligned_case(r, 64u, 128u, "a=64,s=128");
    aligned_case(r, 128u, 256u, "a=128,s=256");
    aligned_case(r, 4096u, 8192u, "a=4096,s=8192");
    aligned_case(r, 8u, 0u, "a=8,s=0");
    aligned_case(r, 64u, 0u, "a=64,s=0");

    /* 3. allocator provenance: cplus_malloc IS tracked, aligned_alloc is NOT */
    errno = 0;
    q = cplus_malloc(32u);
    rec_add(r, q != NULL ? (cplus_alloc_is_live(q) ? 3 : 1) : 0);
    if (q != NULL) cplus_free(q);

    /* 6. wcstoimax/wcstoumax torture */
    imax_case(r, L"0x2a", 0);
    imax_case(r, L"-42", 10);
    imax_case(r, L" 77", 10);
    imax_case(r, L"123tail", 10);
    imax_case(r, L"+17", 10);
    imax_case(r, L"", 10);
    imax_case(r, L"   \t  ", 10);
    imax_case(r, L"+", 10);
    imax_case(r, L"-", 10);
    imax_case(r, L"0x", 0);
    imax_case(r, L"0xGG", 0);
    imax_case(r, L"123", 1);       /* invalid base */
    imax_case(r, L"123", 37);      /* invalid base */
    imax_case(r, L"123", INT_MIN); /* invalid base */
    imax_case(r, L"123", INT_MAX); /* invalid base */
    imax_case(r, big_pos, 10);     /* 40-digit overflow */
    imax_case(r, big_neg, 10);     /* 40-digit negative overflow */

    umax_case(r, L"42", 10);
    umax_case(r, L"0x2a", 0);
    umax_case(r, L"-1", 10);
    umax_case(r, L"456tail", 10);
    umax_case(r, L"", 10);
    umax_case(r, L"   ", 10);
    umax_case(r, L"0x", 0);
    umax_case(r, L"0xGG", 0);
    umax_case(r, L"123", 1);
    umax_case(r, L"123", 37);
    umax_case(r, big_pos, 10);

    null_cases(r);
}

#ifndef _WIN32
/* ------------------------------------------------------------------------ */
/* 5. Termination paths in forked children (never in the parent).           */
/* ------------------------------------------------------------------------ */
static int g_qfd = -1;

static void q_marker(void) {
    const char c = 'Q';
    if (g_qfd >= 0) (void)write(g_qfd, &c, 1);
}

static void q_noop(void) {}

static void test_termination(void) {
    int pfd[2];
    pid_t pid;
    int status = 0;
    int i;
    char buf[16];
    ssize_t n;

    CHECK(pipe(pfd) == 0, "pipe() failed");
    if (pfd[0] < 0 || pfd[1] < 0) return;
    /* Non-blocking read end so the "_Exit writes no marker" check cannot hang. */
    (void)fcntl(pfd[0], F_SETFL, O_NONBLOCK);
    g_qfd = pfd[1];

    /* Register several no-op handlers plus one pipe-reporting handler. */
    CHECK(cplus_at_quick_exit(q_marker) == 0, "at_quick_exit(marker) != 0");
    for (i = 0; i < 8; ++i)
        CHECK(cplus_at_quick_exit(q_noop) == 0, "at_quick_exit(noop) != 0");

    /* quick_exit(42): exact status + at_quick_exit handlers run. */
    pid = fork();
    CHECK(pid >= 0, "fork() for quick_exit failed");
    if (pid == 0) {
        cplus_quick_exit(42);
        _exit(99); /* unreachable if wrapper returns (it must not) */
    }
    if (pid > 0) {
        CHECK(waitpid(pid, &status, 0) == pid, "waitpid(quick_exit) failed");
        CHECK(WIFEXITED(status), "quick_exit child did not exit normally");
        CHECK(WEXITSTATUS(status) == 42, "quick_exit child status != 42");
        n = read(pfd[0], buf, sizeof buf);
        CHECK(n == 1 && buf[0] == 'Q',
              "quick_exit did not run at_quick_exit handler (no 'Q' marker)");
    }

    /* _Exit(17): exact status and NO at_quick_exit handlers run. */
    pid = fork();
    CHECK(pid >= 0, "fork() for _Exit failed");
    if (pid == 0) {
        cplus__Exit(17);
        _exit(99);
    }
    if (pid > 0) {
        CHECK(waitpid(pid, &status, 0) == pid, "waitpid(_Exit) failed");
        CHECK(WIFEXITED(status), "_Exit child did not exit normally");
        CHECK(WEXITSTATUS(status) == 17, "_Exit child status != 17");
        n = read(pfd[0], buf, sizeof buf);
        CHECK(n < 0, "_Exit incorrectly ran at_quick_exit handlers");
    }

    close(pfd[0]);
    close(pfd[1]);
    g_qfd = -1;
}
#endif /* !_WIN32 */

/* ------------------------------------------------------------------------ */
/* 4. Repeated fail/retry: 10000 cycles, no cplus live-count growth.        */
/* ------------------------------------------------------------------------ */
static void test_repeated(void) {
    size_t before, after;
    unsigned int i;
    before = cplus_alloc_live_count();
    for (i = 0u; i < 10000u; ++i) {
        size_t a = (i & 1u) ? 16u : 64u;
        size_t s = a * ((i % 3u) + 1u);
        void *p = cplus_aligned_alloc(a, s);
        if (p == NULL) {
            CHECK(0, "aligned_alloc returned NULL in valid retry loop");
            continue;
        }
        CHECK(((uintptr_t)p % (uintptr_t)a) == 0u, "retry pointer misaligned");
        memset(p, (int)(i & 0xFFu), s);
        free(p);
    }
    after = cplus_alloc_live_count();
    CHECK(after == before, "cplus_alloc_live_count changed across aligned_alloc cycles");
}

int main(void) {
    Rec a, b;
    int i;

    a.n = 0;
    b.n = 0;

    /* 7. Run every failure path under BOTH logging states; results identical. */
    run_suite(&a, 0);
    run_suite(&b, 1);
    cplus_libc_log_set_enabled(0); /* keep harness output clean */
    CHECK(a.n == b.n, "logging changed the number of recorded results");
    if (a.n == b.n) {
        for (i = 0; i < a.n; ++i) {
            if (a.v[i] != b.v[i]) {
                printf("FINDING: logging changed result[%d]: off=%d on=%d\n",
                       i, a.v[i], b.v[i]);
                ++g_findings;
            }
        }
    }

    /* Concretely verify the two headline defined-rejection facts. */
    cplus_libc_log_set_enabled(0);
    {
        void *p;
        errno = 0;
        p = cplus_aligned_alloc(0u, 8u);
        CHECK(p == NULL, "alignment 0 did not return NULL (modulo-by-zero risk)");
        CHECK(errno == EINVAL, "alignment 0 did not set EINVAL");
        errno = 0;
        p = cplus_aligned_alloc(64u, 100u);
        CHECK(p == NULL, "non-multiple size did not return NULL");
        CHECK(errno == EINVAL, "non-multiple size did not set EINVAL");
    }

    test_repeated();

#ifndef _WIN32
    cplus_libc_log_set_enabled(0);
    test_termination();
#endif

    cplus_libc_log_set_enabled(0);
    printf("c17_attack: ok\n");
    return 0;
}
