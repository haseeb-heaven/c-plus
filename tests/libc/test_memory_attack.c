/*
 * Adversarial memory/string attack test for the C+ hardened libc.
 *
 * Standalone C99. Only DEFINED inputs are used: no raw overflow, no
 * cplus_strlen(NULL), no overlapping raw memcpy. Checked APIs live in
 * build/safe/libcplus_safecrt.a and raw APIs in build/libc/libcplus_hardened.a.
 *
 * Build (from repo root):
 *   sh tests/libc/build_libc.sh >/dev/null
 *   sh scripts/build_safecrt.sh >/dev/null
 *   clang -std=c99 -Wall -Wextra -pedantic -Ilibc -Ilibc/include \
 *     -Istdlib/safe -Ithird_party/safeclib/include \
 *     -Ithird_party/intel-safestringlib/include \
 *     tests/libc/test_memory_attack.c \
 *     build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
 *     -lm -pthread -o /tmp/test_memory_attack
 *
 * FINDING: lines are emitted whenever an asserted contract is violated.
 * Guard-page subtests run in forked children so a real out-of-bounds fault is
 * reported with its signal instead of silently killing the whole run.
 */
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

/* Raw C+ libc entry points (see libc/install-include/cplus_libc.h). */
extern void *cplus_memcpy(void *, const void *, size_t);
extern void *cplus_memmove(void *, const void *, size_t);
extern void *cplus_memset(void *, int, size_t);
extern int cplus_memcmp(const void *, const void *, size_t);
extern void *cplus_memchr(const void *, int, size_t);
extern void *cplus_memrchr(const void *, int, size_t);
extern size_t cplus_strlen(const char *);
extern size_t cplus_strnlen(const char *, size_t);
extern char *cplus_strcpy(char *, const char *);
extern char *cplus_strncpy(char *, const char *, size_t);
extern char *cplus_strcat(char *, const char *);
extern char *cplus_strncat(char *, const char *, size_t);
extern int cplus_strcmp(const char *, const char *);
extern int cplus_strncmp(const char *, const char *, size_t);
extern char *cplus_strchr(const char *, int);
extern char *cplus_strrchr(const char *, int);
extern char *cplus_strstr(const char *, const char *);

/* Checked APIs: ABI is int / size_t, declared extern per task instructions.
 * Signatures mirror stdlib/safe/safe_memory.h and safe_string.h. */
typedef int cplus_errno_t;
typedef size_t cplus_rsize_t;
extern cplus_errno_t cplus_memcpy_s(void *, cplus_rsize_t, const void *, cplus_rsize_t);
extern cplus_errno_t cplus_memmove_s(void *, cplus_rsize_t, const void *, cplus_rsize_t);
extern cplus_errno_t cplus_memset_s(void *, cplus_rsize_t, int, cplus_rsize_t);
extern cplus_errno_t cplus_memcmp_s(const void *, cplus_rsize_t, const void *, cplus_rsize_t, int *);
extern cplus_errno_t cplus_memzero_s(void *, cplus_rsize_t);
extern cplus_errno_t cplus_strcpy_s(char *, cplus_rsize_t, const char *);
extern cplus_errno_t cplus_strncpy_s(char *, cplus_rsize_t, const char *, cplus_rsize_t);
extern cplus_errno_t cplus_strcat_s(char *, cplus_rsize_t, const char *);
extern cplus_errno_t cplus_strncat_s(char *, cplus_rsize_t, const char *, cplus_rsize_t);
extern cplus_rsize_t cplus_strnlen_s(const char *, cplus_rsize_t);

/* Constraint mode must be IGNORE so violations are reported via return codes. */
typedef enum {
    CPLUS_SAFE_CONSTRAINT_IGNORE = 0,
    CPLUS_SAFE_CONSTRAINT_ABORT = 1
} cplus_safe_constraint_mode;
extern void cplus_safe_set_constraint_mode(cplus_safe_constraint_mode);
extern cplus_safe_constraint_mode cplus_safe_get_constraint_mode(void);

/* safeclib constraint codes (NOT platform errno), from safe_lib_errno.h. */
#define ESNULLP 400
#define ESZEROL 401
#define ESLEMAX 403
#define ESOVRLP 404
#define ESNOSPC 406
#define ESUNTERM 407

static int g_failures = 0;
static int g_fork_findings = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                          \
            printf("FINDING: %s\n", (msg));                                     \
            ++g_failures;                                                       \
        }                                                                      \
    } while (0)

#define CHECK_ERR(name, got, want)                                             \
    do {                                                                       \
        cplus_errno_t _got = (got);                                             \
        cplus_errno_t _want = (want);                                           \
        if (_got != _want) {                                                    \
            printf("FINDING: %s returned %d, expected %d\n", (name),            \
                   (int)_got, (int)_want);                                      \
            ++g_failures;                                                       \
        }                                                                      \
    } while (0)

/* ---------- canary helpers ---------- */
#define CANARY 0xA5u
#define PRE_N 16u
#define POST_N 16u
#define BUF_N 64u

typedef struct {
    unsigned char pre[PRE_N];
    unsigned char buf[BUF_N];
    unsigned char post[POST_N];
} guard_buf;

static void gb_init(guard_buf *g) {
    memset(g, (int)CANARY, sizeof(*g));
}

static int gb_ok(const guard_buf *g) {
    size_t i;
    for (i = 0; i < PRE_N; ++i)
        if (g->pre[i] != CANARY) return 0;
    for (i = 0; i < POST_N; ++i)
        if (g->post[i] != CANARY) return 0;
    return 1;
}

static int all_bytes(const unsigned char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != v) return 0;
    return 1;
}

/* ---------- 1. exact-size and too-small-dmax boundary ---------- */
static void test_exact_and_overflow(void) {
    unsigned char src[BUF_N + 1];
    char strsrc[BUF_N + 2];
    size_t i;
    guard_buf g;
    cplus_errno_t r;

    for (i = 0; i < sizeof(src); ++i) src[i] = (unsigned char)(i * 7u + 1u);
    for (i = 0; i < BUF_N; ++i) strsrc[i] = 'A' + (char)(i % 26);
    strsrc[BUF_N] = '\0';
    strsrc[BUF_N + 1] = '\0';

    /* Valid exact-size raw copy into an exactly-sized destination. */
    gb_init(&g);
    CHECK(cplus_memcpy(g.buf, src, BUF_N) == g.buf, "raw memcpy return value");
    CHECK(memcmp(g.buf, src, BUF_N) == 0, "exact-size raw memcpy content");
    CHECK(gb_ok(&g), "exact-size raw memcpy corrupted a canary");

    /* Valid exact-size checked copy. */
    gb_init(&g);
    r = cplus_memcpy_s(g.buf, BUF_N, src, BUF_N);
    CHECK_ERR("memcpy_s exact size", r, 0);
    CHECK(memcmp(g.buf, src, BUF_N) == 0, "exact-size memcpy_s content");
    CHECK(gb_ok(&g), "exact-size memcpy_s corrupted a canary");

    /* n+1 into an n-byte destination: defined ESNOSPC, no canary damage.
     * C+ SECURITY PATCH: destination remains intact on refusal (dmax untrusted). */
    gb_init(&g);
    r = cplus_memcpy_s(g.buf, BUF_N, src, BUF_N + 1u);
    CHECK_ERR("memcpy_s undersized dmax", r, ESNOSPC);
    CHECK(all_bytes(g.buf, BUF_N, (unsigned char)CANARY), "memcpy_s ESNOSPC modified destination on refusal");
    CHECK(gb_ok(&g), "memcpy_s undersized dmax corrupted a canary");

    /* Same boundary for memset_s: clamps to dmax, first dmax bytes set. */
    gb_init(&g);
    r = cplus_memset_s(g.buf, BUF_N, 0x5Au, BUF_N + 1u);
    CHECK_ERR("memset_s undersized dmax", r, ESNOSPC);
    CHECK(all_bytes(g.buf, BUF_N, 0x5Au), "memset_s ESNOSPC did not clamp to dmax");
    CHECK(gb_ok(&g), "memset_s undersized dmax corrupted a canary");

    /* Same boundary for memmove_s: destination remains intact on refusal. */
    gb_init(&g);
    r = cplus_memmove_s(g.buf, BUF_N, src, BUF_N + 1u);
    CHECK_ERR("memmove_s undersized dmax", r, ESNOSPC);
    CHECK(all_bytes(g.buf, BUF_N, (unsigned char)CANARY), "memmove_s ESNOSPC modified destination on refusal");
    CHECK(gb_ok(&g), "memmove_s undersized dmax corrupted a canary");

    /* strcpy_s with a source whose length exactly equals dmax (needs dmax+1). */
    gb_init(&g);
    r = cplus_strcpy_s((char *)g.buf, BUF_N, strsrc);
    CHECK_ERR("strcpy_s undersized dmax", r, ESNOSPC);
    CHECK(gb_ok(&g), "strcpy_s undersized dmax corrupted a canary");
}

/* ---------- 2. overlapping buffers ---------- */
static void test_overlap(void) {
    char raw[32];
    guard_buf g;
    cplus_errno_t r;
    size_t i;

    /* Raw memmove with overlap (defined) must produce the memmove result. */
    for (i = 0; i < sizeof(raw); ++i) raw[i] = (char)('a' + i);
    cplus_memmove(raw + 3, raw, 20);
    for (i = 0; i < 20; ++i)
        CHECK(raw[3 + i] == (char)('a' + i), "raw memmove overlap (dst>src) wrong");

    for (i = 0; i < sizeof(raw); ++i) raw[i] = (char)('a' + i);
    cplus_memmove(raw, raw + 3, 20);
    for (i = 0; i < 20; ++i)
        CHECK(raw[i] == (char)('a' + 3 + i), "raw memmove overlap (dst<src) wrong");

    /* memmove_s explicitly permits overlap and must still be correct. */
    gb_init(&g);
    for (i = 0; i < 20; ++i) g.buf[i] = (char)('a' + i);
    r = cplus_memmove_s(g.buf + 3, 20, g.buf, 20);
    CHECK_ERR("memmove_s overlap", r, 0);
    for (i = 0; i < 20; ++i)
        CHECK(g.buf[3 + i] == (char)('a' + i), "memmove_s overlap wrong content");
    CHECK(gb_ok(&g), "memmove_s overlap corrupted a canary");

    /* Raw memcpy with overlap is UB, so test only the CHECKED API: it must
     * reject ESOVRLP. Documented state: safeclib zeroes dmax bytes of dest. */
    gb_init(&g);
    for (i = 0; i < 20; ++i) g.buf[i] = (char)('B' + (int)i);
    r = cplus_memcpy_s(g.buf + 1, 16, g.buf, 8);
    CHECK_ERR("memcpy_s overlapping regions", r, ESOVRLP);
    CHECK(gb_ok(&g), "memcpy_s overlap rejection corrupted a canary");
}

/* ---------- 3. unterminated sources, guard page at end ----------
 * Each probe runs in a forked child. If a function reads src[n] (one byte
 * past the declared bound) it faults on the PROT_NONE page and the parent
 * records a FINDING with the exact signal instead of crashing the suite. */
static char *g_src;
static size_t g_n;

static void guarded_probe(int cas) {
    char dst[80];
    cplus_errno_t r;

    switch (cas) {
    case 0:
        CHECK(cplus_strnlen(g_src, g_n) == g_n, "raw strnlen wrong count on unterminated source");
        break;
    case 1:
        CHECK(cplus_strnlen_s(g_src, g_n) == g_n, "checked strnlen_s wrong count on unterminated source");
        break;
    case 2:
        CHECK(cplus_memchr(g_src, 'Q', g_n) == NULL, "raw memchr wrong result at bound");
        break;
    case 3:
        CHECK(cplus_memrchr(g_src, 'Z', g_n) == g_src + g_n - 1, "raw memrchr wrong result at bound");
        break;
    case 4:
        memset(dst, 0xCC, sizeof(dst));
        CHECK(cplus_strncpy(dst, g_src, g_n) == dst, "raw strncpy return value");
        CHECK(memcmp(dst, g_src, g_n) == 0, "raw strncpy content");
        CHECK((unsigned char)dst[g_n] == 0xCCu, "raw strncpy wrote past n bytes");
        break;
    case 5:
        /* Checked strncpy with honest capacities: dmax=n+1, slen=n. */
        memset(dst, 0xCC, sizeof(dst));
        r = cplus_strncpy_s(dst, g_n + 1u, g_src, g_n);
        CHECK_ERR("strncpy_s bounded unterminated source", r, 0);
        CHECK(memcmp(dst, g_src, g_n) == 0, "strncpy_s bounded content");
        CHECK(dst[g_n] == '\0', "strncpy_s must terminate");
        CHECK((unsigned char)dst[g_n + 1u] == 0xCCu, "strncpy_s wrote past dmax");
        break;
    case 6:
        /* Too-small dmax: defined ESNOSPC; byte n (outside dmax) untouched. */
        memset(dst, 0xCC, sizeof(dst));
        r = cplus_strncpy_s(dst, g_n, g_src, g_n);
        CHECK_ERR("strncpy_s undersized dmax on unterminated source", r, ESNOSPC);
        /* C11 K.3.7.1.4 requires only dest[0] = '\0' on a constraint
         * violation. This build has SAFECLIB_STR_NULL_SLACK disabled, so
         * handle_error() leaves the partial copy in dest[1..dmax-1]; that is
         * conforming, so it is reported as an observation, not a FINDING. */
        CHECK(dst[0] == '\0', "strncpy_s ESNOSPC must leave a terminated destination");
        if (!all_bytes((unsigned char *)dst, g_n, 0))
            printf("OBSERVATION: strncpy_s ESNOSPC leaves partial copy in dest[1..dmax-1] (only dest[0] NUL; canary intact)\n");
        CHECK((unsigned char)dst[g_n] == 0xCCu, "strncpy_s wrote past dmax on rejection");
        break;
    default:
        break;
    }
}

static void run_guarded(const char *name, int cas) {
    pid_t pid = fork();
    int status;

    if (pid == 0) {
        setvbuf(stdout, NULL, _IONBF, 0);
        g_failures = 0;
        guarded_probe(cas);
        _exit(g_failures > 127 ? 127 : g_failures);
    }
    if (pid < 0) {
        printf("FINDING: %s fork failed\n", name);
        ++g_failures;
        return;
    }
    if (waitpid(pid, &status, 0) < 0) {
        printf("FINDING: %s waitpid failed\n", name);
        ++g_failures;
        return;
    }
    if (WIFSIGNALED(status)) {
        printf("FINDING: %s crashed with signal %d (read past declared bound)\n",
               name, WTERMSIG(status));
        ++g_failures;
    } else if (WEXITSTATUS(status) != 0) {
        /* Child already printed its own FINDING lines. */
        g_fork_findings += WEXITSTATUS(status);
    }
}

static void test_unterminated(void) {
    static const char *names[] = {
        "raw strnlen bound", "checked strnlen_s bound", "raw memchr bound",
        "raw memrchr bound", "raw strncpy bound", "checked strncpy_s bound",
        "checked strncpy_s undersized dmax"
    };
    long ps = sysconf(_SC_PAGESIZE);
    size_t page = (ps > 0) ? (size_t)ps : 4096u;
    char *mem;
    size_t i;

    mem = (char *)mmap(NULL, page * 2u, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mem == (char *)MAP_FAILED) {
        printf("note: guard page unavailable; unterminated subtest skipped\n");
        return;
    }
    if (mprotect(mem + page, page, PROT_NONE) != 0) {
        munmap(mem, page * 2u);
        printf("note: guard page unavailable; unterminated subtest skipped\n");
        return;
    }

    /* Source occupies the last n bytes of the readable page; the following
     * page is PROT_NONE, so any read at src[n] faults. */
    g_n = 64u;
    g_src = mem + page - g_n;
    memset(g_src, 'Z', g_n);

    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        run_guarded(names[i], (int)i);

    mprotect(mem + page, page, PROT_READ | PROT_WRITE);
    munmap(mem, page * 2u);
}

/* ---------- 4. zero lengths and NULL (defined cases only) ---------- */
static void test_zero_and_null(void) {
    unsigned char z[8];
    guard_buf g;
    int diff = 12345;
    cplus_errno_t r;

    /* Raw zero-length operations are no-ops; no NULL is passed to raw APIs. */
    memset(z, 0xCC, sizeof(z));
    CHECK(cplus_memcpy(z, z, 0) == z, "raw memcpy zero-length return");
    CHECK(cplus_memmove(z, z, 0) == z, "raw memmove zero-length return");
    CHECK(cplus_memset(z, 0x11, 0) == z, "raw memset zero-length return");
    CHECK(cplus_memcmp(z, z, 0) == 0, "raw memcmp zero-length");
    CHECK(all_bytes(z, sizeof(z), 0xCCu), "zero-length raw op modified memory");

    /* cplus_strlen(NULL) is UB by the C standard. A wrapper cannot make it
     * safe: the compiler is entitled to assume a non-NULL argument and may
     * optimize away any defensive check, and there is no error return to
     * report. This test therefore never calls it; use cplus_strnlen_s. */

    /* NULL arguments to CHECKED APIs must be defined, never crash. */
    gb_init(&g);
    CHECK_ERR("memcpy_s NULL dest", cplus_memcpy_s(NULL, 16, g.buf, 4), ESNULLP);
    CHECK_ERR("memcpy_s NULL src", cplus_memcpy_s(g.buf, 16, NULL, 4), ESNULLP);
    CHECK(gb_ok(&g), "memcpy_s NULL src corrupted a canary");
    CHECK_ERR("memmove_s NULL dest", cplus_memmove_s(NULL, 16, g.buf, 4), ESNULLP);
    CHECK_ERR("memset_s NULL dest", cplus_memset_s(NULL, 16, 0, 4), ESNULLP);
    CHECK_ERR("memcmp_s NULL first", cplus_memcmp_s(NULL, 16, g.buf, 4, &diff), ESNULLP);
    CHECK_ERR("strcpy_s NULL dest", cplus_strcpy_s(NULL, 16, "x"), ESNULLP);
    CHECK_ERR("strcpy_s NULL src", cplus_strcpy_s((char *)g.buf, 16, NULL), ESNULLP);
    CHECK(gb_ok(&g), "strcpy_s NULL src corrupted a canary");

    /* strcat_s requires a valid NUL-terminated destination. */
    gb_init(&g);
    g.buf[0] = '\0';
    CHECK_ERR("strcat_s NULL src", cplus_strcat_s((char *)g.buf, 16, NULL), ESNULLP);
    CHECK(gb_ok(&g), "strcat_s NULL src corrupted a canary");

    /* dmax == 0 with non-zero length is a defined ESZEROL. */
    gb_init(&g);
    r = cplus_memcpy_s(g.buf, 0, g.buf, 1);
    CHECK_ERR("memcpy_s dmax zero", r, ESZEROL);
    CHECK(gb_ok(&g), "memcpy_s dmax zero touched destination");

    /* slen == 0 is explicitly allowed and must be a no-op (EOK). */
    gb_init(&g);
    r = cplus_memcpy_s(g.buf, 16, g.buf, 0);
    CHECK_ERR("memcpy_s slen zero", r, 0);
    CHECK(all_bytes(g.buf, BUF_N, CANARY), "memcpy_s slen zero modified destination");
    CHECK(gb_ok(&g), "memcpy_s slen zero corrupted a canary");

    /* strnlen_s is a bounded query, not an errno_t API: defined results. */
    CHECK(cplus_strnlen_s(NULL, 16) == 0, "strnlen_s NULL must return 0");
    CHECK(cplus_strnlen_s("abc", 0) == 0, "strnlen_s smax 0 must return 0");
    CHECK(cplus_strnlen_s("abc", 16) == 3, "strnlen_s normal length");
}

/* ---------- 5. huge lengths ---------- */
static void test_huge_lengths(void) {
    const cplus_rsize_t huge = (cplus_rsize_t)SIZE_MAX;
    unsigned char src[BUF_N];
    guard_buf g;
    cplus_errno_t r;
    size_t i;

    for (i = 0; i < BUF_N; ++i) src[i] = (unsigned char)(i + 3u);

    gb_init(&g);
    r = cplus_memcpy_s(g.buf, BUF_N, src, huge);
    CHECK_ERR("memcpy_s SIZE_MAX", r, ESLEMAX);
    CHECK(gb_ok(&g), "memcpy_s SIZE_MAX corrupted a canary");
    /* C+ SECURITY PATCH: on constraint refusal dmax-wipe is skipped so an
     * untrusted dmax cannot cause an OOB write; destination remains intact. */
    CHECK(all_bytes(g.buf, BUF_N, (unsigned char)CANARY),
          "memcpy_s SIZE_MAX modified destination on refusal");

    gb_init(&g);
    r = cplus_memmove_s(g.buf, BUF_N, src, huge);
    CHECK_ERR("memmove_s SIZE_MAX", r, ESLEMAX);
    CHECK(gb_ok(&g), "memmove_s SIZE_MAX corrupted a canary");

    /* memset_s clamps n to dmax before returning the error. */
    gb_init(&g);
    r = cplus_memset_s(g.buf, BUF_N, 0x33, huge);
    CHECK_ERR("memset_s SIZE_MAX", r, ESLEMAX);
    CHECK(all_bytes(g.buf, BUF_N, 0x33), "memset_s SIZE_MAX did not clamp to dmax");
    CHECK(gb_ok(&g), "memset_s SIZE_MAX corrupted a canary");

    gb_init(&g);
    r = cplus_strcpy_s((char *)g.buf, huge, "abc");
    CHECK_ERR("strcpy_s SIZE_MAX dmax", r, ESLEMAX);
    CHECK(gb_ok(&g), "strcpy_s SIZE_MAX corrupted a canary");

    gb_init(&g);
    r = cplus_strncpy_s((char *)g.buf, BUF_N, "abc", huge);
    CHECK_ERR("strncpy_s SIZE_MAX slen", r, ESLEMAX);
    CHECK(gb_ok(&g), "strncpy_s SIZE_MAX corrupted a canary");

    gb_init(&g);
    g.buf[0] = '\0';
    r = cplus_strcat_s((char *)g.buf, huge, "x");
    CHECK_ERR("strcat_s SIZE_MAX dmax", r, ESLEMAX);
    CHECK(gb_ok(&g), "strcat_s SIZE_MAX corrupted a canary");

    gb_init(&g);
    g.buf[0] = '\0';
    r = cplus_strncat_s((char *)g.buf, huge, "x", huge);
    CHECK(r != 0, "strncat_s SIZE_MAX must be rejected");
    CHECK(gb_ok(&g), "strncat_s SIZE_MAX corrupted a canary");

    CHECK(cplus_strnlen_s("abc", huge) == 0, "strnlen_s SIZE_MAX must return 0");
}

/* ---------- 7. differential against host libc ---------- */
static unsigned long rnd_state = 0xC0FFEE12UL;
static unsigned long rnd(void) {
    rnd_state = rnd_state * 1103515245UL + 12345UL;
    return rnd_state >> 8;
}
static int sgn(int v) { return (v > 0) - (v < 0); }
static ptrdiff_t off(const char *base, const char *p) {
    return p ? (ptrdiff_t)(p - base) : (ptrdiff_t)-1;
}

static void test_differential(void) {
    char a[48], b[48];
    int iter;

    for (iter = 0; iter < 1000; ++iter) {
        size_t la = (size_t)(rnd() % 40u);
        size_t lb = (size_t)(rnd() % 40u);
        size_t i, n;

        /* Non-NUL bytes 1..255 so high-bit unsigned comparison is exercised. */
        for (i = 0; i < la; ++i) a[i] = (char)(1u + (unsigned char)(rnd() % 255u));
        for (i = 0; i < lb; ++i) b[i] = (char)(1u + (unsigned char)(rnd() % 255u));
        a[la] = '\0';
        b[lb] = '\0';

        if (sgn(cplus_strcmp(a, b)) != sgn(strcmp(a, b))) {
            printf("FINDING: strcmp mismatch at iter %d\n", iter);
            ++g_failures;
        }
        n = (size_t)(rnd() % 46u);
        if (sgn(cplus_strncmp(a, b, n)) != sgn(strncmp(a, b, n))) {
            printf("FINDING: strncmp mismatch at iter %d (n=%lu)\n", iter,
                   (unsigned long)n);
            ++g_failures;
        }
        if (off(a, cplus_strstr(a, b)) != off(a, strstr(a, b))) {
            printf("FINDING: strstr mismatch at iter %d\n", iter);
            ++g_failures;
        }
        n = 1u + (size_t)(rnd() % 40u);
        if (sgn(cplus_memcmp(a, b, n)) != sgn(memcmp(a, b, n))) {
            printf("FINDING: memcmp mismatch at iter %d (n=%lu)\n", iter,
                   (unsigned long)n);
            ++g_failures;
        }

        {
            int ch = (int)(1u + (unsigned char)(rnd() % 255u));
            if (off(a, cplus_strchr(a, ch)) != off(a, strchr(a, ch))) {
                printf("FINDING: strchr mismatch at iter %d\n", iter);
                ++g_failures;
            }
            if (off(a, cplus_strrchr(a, ch)) != off(a, strrchr(a, ch))) {
                printf("FINDING: strrchr mismatch at iter %d\n", iter);
                ++g_failures;
            }
            if (off(a, cplus_memchr(a, ch + 256, la)) !=
                off(a, memchr(a, ch, la))) {
                printf("FINDING: memchr mismatch at iter %d\n", iter);
                ++g_failures;
            }
            /* memrchr has no portable host equivalent on all platforms;
             * verify it against an equivalent forward last-match search. */
            {
                const void *want = NULL;
                size_t k;
                for (k = 0; k < la; ++k)
                    if ((unsigned char)a[k] == (unsigned char)ch) want = a + k;
                if (cplus_memrchr(a, ch + 256, la) != want) {
                    printf("FINDING: memrchr mismatch at iter %d\n", iter);
                    ++g_failures;
                }
            }
        }
    }

    /* Explicit NUL / high-bit / empty-needle edge cases. */
    CHECK(cplus_strchr("abc", 0) != NULL, "strchr must find terminating NUL");
    CHECK(cplus_strrchr("abc", 0) != NULL, "strrchr must find terminating NUL");
    CHECK(cplus_strchr("abc", 256) == cplus_strchr("abc", 0),
          "strchr must convert int to char (256 -> NUL)");
    CHECK(cplus_strstr("abc", "") != NULL, "empty needle must match haystack");
    CHECK(cplus_strcmp("\x80", "\x7f") > 0, "strcmp must compare unsigned chars");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); /* keep FINDING output before any crash */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    CHECK(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE,
          "constraint mode is not IGNORE");

    test_exact_and_overflow();
    test_overlap();
    test_unterminated();
    test_zero_and_null();
    test_huge_lengths();
    test_differential();

    if (g_failures != 0 || g_fork_findings != 0) {
        printf("memory_attack: FAILED (%d finding(s), %d child finding(s))\n",
               g_failures, g_fork_findings);
        return 1;
    }
    printf("memory_attack: ok\n");
    return 0;
}
