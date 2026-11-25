/* Runtime scenarios for ordinary strcpy / strncpy hardened by safe_c.h.
 *
 * Standalone C99.  Compile and link like the other scenario fixtures:
 *   clang -O2 -std=c99 -I. -Istdlib/safe -Ilibc -Ithird_party/safeclib/include \
 *         tests/scenarios/runtime_05_strcpy.c \
 *         build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *         -lm -pthread
 *
 * Every refused operation below is arranged so that, even if the check were
 * missing, the write would land inside the same enclosing object: the tests
 * never rely on genuine undefined behaviour to observe a refusal.  The raw
 * (untracked) host allocation is made before any tracked free so the recent
 * free cache cannot produce a false use-after-free on that address.
 */
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_stats.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* ---- raw host allocator (for the "unknown capacity" scenarios) -------- */
#undef malloc
#undef free
extern void *malloc(size_t);
extern void free(void *);
static void *host_alloc(size_t n) { return malloc(n); }
static void host_release(void *p) { free(p); }
#define malloc(sz) malloc_s(sz)
#define free(p) free_s(p)

static volatile size_t g_host_size = 37; /* volatile: object size stays unknown */
static char *g_host;                     /* allocated first, released last */

#define SCENARIO_COUNT 26

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            fprintf(stderr, "runtime_strcpy: check failed at line %d: %s\n",   \
                    __LINE__, #cond);                                          \
        }                                                                      \
    } while (0)

static void fill_bytes(unsigned char *p, size_t n, unsigned char v)
{
    size_t i;
    for (i = 0; i < n; ++i) p[i] = v;
}

static int canary_ok(const unsigned char *p, size_t n, unsigned char v)
{
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != v) return 0;
    return 1;
}

static struct {
    char buf[16];
    char post[4];
} g_global_dest;

/* 01: exact fit - source length is capacity-1 (7 chars into buf[8]). */
static void sc_05_exact_fit(void)
{
    struct { char buf[8]; char post[4]; } s;
    char *r;
    fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
    errno = 0;
    r = strcpy(s.buf, "1234567");
    CHECK(r == s.buf);
    CHECK(errno == 0);
    CHECK(strcmp(s.buf, "1234567") == 0);
    CHECK(s.buf[7] == '\0');
    CHECK(canary_ok((const unsigned char *)s.post, sizeof s.post, 0x5a));
}

/* 02: one too long - refused with ERANGE, safeclib clears the destination. */
static void sc_05_one_too_long_refused(void)
{
    struct { char buf[8]; char post[4]; } s;
    char *r;
    fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
    errno = 0;
    r = strcpy(s.buf, "12345678");
    CHECK(r == NULL);
    CHECK(errno == ERANGE);
    CHECK(s.buf[0] == '\0');
    CHECK(canary_ok((const unsigned char *)s.post, sizeof s.post, 0x5a));
}

/* 03: empty string - one NUL written, untouched tail stays intact. */
static void sc_05_empty_string(void)
{
    struct { char buf[8]; } s;
    char *r;
    fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
    errno = 0;
    r = strcpy(s.buf, "");
    CHECK(r == s.buf);
    CHECK(errno == 0);
    CHECK(s.buf[0] == '\0');
    CHECK((unsigned char)s.buf[1] == 0x5a);
}

/* 04: source with an embedded NUL - copy stops at the first NUL. */
static void sc_05_embedded_nul(void)
{
    char src[] = {'a', 'b', '\0', 'x', 'y', '\0'};
    struct { char buf[8]; } s;
    char *r;
    fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
    errno = 0;
    r = strcpy(s.buf, src);
    CHECK(r == s.buf);
    CHECK(errno == 0);
    CHECK(strcmp(s.buf, "ab") == 0);
    CHECK((unsigned char)s.buf[2] == '\0');
    CHECK((unsigned char)s.buf[3] == 0x5a);
}

/* 05: unterminated source with an explicit bounded alternative.  A raw
 * strcpy would run strlen past the end; the bounded strncpy_s form is used
 * instead and must produce a terminated result. */
static void sc_05_unterminated_bounded_alternative(void)
{
    char *src = malloc(8);
    char *dst = malloc(8);
    errno_t rc;
    if (src == NULL || dst == NULL) {
        CHECK(0);
        return;
    }
    fill_bytes((unsigned char *)src, 8, 0);
    memcpy(src, "abcd", 4); /* no NUL anywhere in src */
    fill_bytes((unsigned char *)dst, 8, 0x5a);
    errno = 0;
    rc = strncpy_s(dst, 8, src, 4);
    CHECK(rc == 0);
    CHECK(errno == 0);
    CHECK(dst[0] == 'a' && dst[1] == 'b' && dst[2] == 'c' && dst[3] == 'd');
    CHECK(dst[4] == '\0');
    CHECK((unsigned char)dst[5] == 0x5a);
    free(src);
    free(dst);
}

/* 06: NULL destination is refused without dereferencing it (EINVAL). */
static void sc_05_null_dest(void)
{
    char *r;
    errno = 0;
    r = strcpy(NULL, "x");
    CHECK(r == NULL);
    CHECK(errno == EINVAL);
}

/* 07: NULL source is refused without calling strlen on it (EINVAL). */
static void sc_05_null_src(void)
{
    char buf[8];
    char *r;
    fill_bytes((unsigned char *)buf, sizeof buf, 0x5a);
    errno = 0;
    r = strcpy(buf, NULL);
    CHECK(r == NULL);
    CHECK(errno == EINVAL);
    CHECK((unsigned char)buf[0] == 0x5a);
}

/* 08: stack destination, leading and trailing canaries both intact. */
static void sc_05_stack_dest_canaries(void)
{
    struct { char pre[4]; char buf[16]; char post[4]; } s;
    char *r;
    fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
    errno = 0;
    r = strcpy(s.buf, "stack-copy");
    CHECK(r == s.buf);
    CHECK(errno == 0);
    CHECK(strcmp(s.buf, "stack-copy") == 0);
    CHECK(canary_ok((const unsigned char *)s.pre, sizeof s.pre, 0x5a));
    CHECK(canary_ok((const unsigned char *)s.post, sizeof s.post, 0x5a));
}

/* 09: file-scope (global) destination is bounded too. */
static void sc_05_global_dest(void)
{
    char *r;
    fill_bytes((unsigned char *)&g_global_dest, sizeof g_global_dest, 0x5a);
    errno = 0;
    r = strcpy(g_global_dest.buf, "global");
    CHECK(r == g_global_dest.buf);
    CHECK(errno == 0);
    CHECK(strcmp(g_global_dest.buf, "global") == 0);
    CHECK(canary_ok((const unsigned char *)g_global_dest.post,
                    sizeof g_global_dest.post, 0x5a));
}

/* 10: struct member destination with integer canaries on each side. */
static void sc_05_struct_member_canary(void)
{
    struct { unsigned int pre; char buf[8]; unsigned int post; } s;
    char *r;
    s.pre = 0x12345678u;
    s.post = 0xdeadbeefu;
    fill_bytes((unsigned char *)s.buf, sizeof s.buf, 0x5a);
    errno = 0;
    r = strcpy(s.buf, "abc");
    CHECK(r == s.buf);
    CHECK(errno == 0);
    CHECK(strcmp(s.buf, "abc") == 0);
    CHECK(s.pre == 0x12345678u);
    CHECK(s.post == 0xdeadbeefu);

    fill_bytes((unsigned char *)s.buf, sizeof s.buf, 0x5a);
    errno = 0;
    r = strcpy(s.buf, "abcdefgh");
    CHECK(r == NULL);
    CHECK(errno == ERANGE);
    CHECK(s.pre == 0x12345678u);
    CHECK(s.post == 0xdeadbeefu);
}

/* 11: heap destination from the tracked allocator (malloc -> malloc_s). */
static void sc_05_heap_tracked_dest(void)
{
    char *p = malloc(8);
    char *r;
    if (p == NULL) {
        CHECK(0);
        return;
    }
    CHECK(cplus_safe_ptr_size(p) == 8);
    fill_bytes((unsigned char *)p, 8, 0x5a);
    errno = 0;
    r = strcpy(p, "abc");
    CHECK(r == p);
    CHECK(errno == 0);
    CHECK(strcmp(p, "abc") == 0);

    fill_bytes((unsigned char *)p, 8, 0x5a);
    errno = 0;
    r = strcpy(p, "abcdefgh");
    CHECK(r == NULL);
    CHECK(errno == ERANGE);
    CHECK((unsigned char)p[0] == 0);
    free(p);
}

/* 12: host-malloc destination has unknown capacity; default mode keeps the
 * raw-C behaviour (success) rather than claiming a safety guarantee. */
static void sc_05_host_malloc_unknown(void)
{
    char *r;
    g_host = host_alloc(g_host_size);
    if (g_host == NULL) {
        CHECK(0);
        return;
    }
    CHECK(cplus_safe_ptr_size(g_host) == 0);
    fill_bytes((unsigned char *)g_host, g_host_size, 0x5a);
    errno = 0;
    r = strcpy(g_host, "abc");
    CHECK(r == g_host);
    CHECK(errno == 0);
    CHECK(strcmp(g_host, "abc") == 0);
    CHECK((unsigned char)g_host[4] == 0x5a);
}

/* 13: one-past-the-end of a sub-array.  The pointer still addresses the
 * containing object, so a (hypothetical) unchecked write is not UB; the
 * wrapper must refuse it because mode-1 object size is zero. */
static void sc_05_dest_one_past_end(void)
{
    struct pair { char a[4]; char b[4]; };
    struct pair *s = malloc(sizeof *s);
    char *r;
    if (s == NULL) {
        CHECK(0);
        return;
    }
    fill_bytes((unsigned char *)s, sizeof *s, 0x5a);
    errno = 0;
    r = strcpy(s->a + 4, "xy");
    CHECK(r == NULL);
    CHECK(errno == ERANGE);
    CHECK(canary_ok((const unsigned char *)s->b, sizeof s->b, 0x5a));
    free(s);
}

/* 14: self-copy is a defined no-op and is accepted by strcpy_s. */
static void sc_05_self_copy(void)
{
    char s[16];
    char *r;
    fill_bytes((unsigned char *)s, sizeof s, 0x5a);
    memcpy(s, "abcdefgh", 9);
    errno = 0;
    r = strcpy(s, s);
    CHECK(r == s);
    CHECK(errno == 0);
    CHECK(strcmp(s, "abcdefgh") == 0);
}

/* 15: overlapping source/destination in both directions is refused with an
 * EINVAL-class error; the trailing canary is outside the cleared range. */
static void sc_05_overlap_refused(void)
{
    struct { char buf[16]; char post[4]; } s;
    char *r;
    fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
    memcpy(s.buf, "abcdefgh", 9);

    errno = 0;
    r = strcpy(s.buf + 2, s.buf);
    CHECK(r == NULL);
    CHECK(errno == EINVAL);
    CHECK(canary_ok((const unsigned char *)s.post, sizeof s.post, 0x5a));

    fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
    memcpy(s.buf, "abcdefgh", 9);
    errno = 0;
    r = strcpy(s.buf, s.buf + 2);
    CHECK(r == NULL);
    CHECK(errno == EINVAL);
    CHECK(canary_ok((const unsigned char *)s.post, sizeof s.post, 0x5a));
}

/* 16: very long source into a sufficiently large destination. */
static void sc_05_very_long_source(void)
{
    static char src[2049];
    static char dst[4096];
    char *r;
    size_t i;
    for (i = 0; i < 2048; ++i) src[i] = 'a';
    src[2048] = '\0';
    fill_bytes((unsigned char *)dst, sizeof dst, 0);
    errno = 0;
    r = strcpy(dst, src);
    CHECK(r == dst);
    CHECK(errno == 0);
    CHECK(strlen(dst) == 2048);
    CHECK(dst[2047] == 'a');
    CHECK(dst[2048] == '\0');
}

/* 17: strncpy with n shorter than the source.  ISO C strncpy would leave
 * no terminator; the checked TR-style wrapper always terminates. */
static void sc_05_strncpy_short_n(void)
{
    struct { char buf[8]; char post[4]; } s;
    char *r;
    fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
    errno = 0;
    r = strncpy(s.buf, "abcdef", 3);
    CHECK(r == s.buf);
    CHECK(errno == 0);
    CHECK(s.buf[0] == 'a' && s.buf[1] == 'b' && s.buf[2] == 'c');
    CHECK(s.buf[3] == '\0');
    CHECK((unsigned char)s.buf[4] == 0x5a);
    CHECK(canary_ok((const unsigned char *)s.post, sizeof s.post, 0x5a));
}

/* 18: n == capacity succeeds when the source ends early, but is refused
 * when the source actually fills all n bytes (no room for the NUL). */
static void sc_05_strncpy_n_equals_cap(void)
{
    struct { char buf[8]; char post[4]; } a, b;
    char *r;
    fill_bytes((unsigned char *)&a, sizeof a, 0x5a);
    errno = 0;
    r = strncpy(a.buf, "abc", 8);
    CHECK(r == a.buf);
    CHECK(errno == 0);
    CHECK(strcmp(a.buf, "abc") == 0);
    CHECK(canary_ok((const unsigned char *)a.post, sizeof a.post, 0x5a));

    fill_bytes((unsigned char *)&b, sizeof b, 0x5a);
    errno = 0;
    r = strncpy(b.buf, "abcdefgh", 8);
    CHECK(r == NULL);
    CHECK(errno == ERANGE);
    CHECK((unsigned char)b.buf[0] == 0);
    CHECK(canary_ok((const unsigned char *)b.post, sizeof b.post, 0x5a));
}

/* 19: n greater than capacity is refused with ERANGE. */
static void sc_05_strncpy_n_above_cap(void)
{
    struct { char buf[8]; char post[4]; } s;
    char *r;
    fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
    errno = 0;
    r = strncpy(s.buf, "abcdefghij", 9);
    CHECK(r == NULL);
    CHECK(errno == ERANGE);
    CHECK((unsigned char)s.buf[0] == 0);
    CHECK(canary_ok((const unsigned char *)s.post, sizeof s.post, 0x5a));
}

/* 20: zero n returns the destination untouched (and leaves errno alone). */
static void sc_05_strncpy_zero_n(void)
{
    struct { char buf[8]; } s;
    char *r;
    fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
    errno = 0;
    r = strncpy(s.buf, "abc", 0);
    CHECK(r == s.buf);
    CHECK(errno == 0);
    CHECK((unsigned char)s.buf[0] == 0x5a);
}

/* 21: strncpy NULL arguments follow the same EINVAL contract. */
static void sc_05_strncpy_null_args(void)
{
    char buf[8];
    char *r;
    fill_bytes((unsigned char *)buf, sizeof buf, 0x5a);
    errno = 0;
    r = strncpy(NULL, "x", 2);
    CHECK(r == NULL);
    CHECK(errno == EINVAL);
    errno = 0;
    r = strncpy(buf, NULL, 2);
    CHECK(r == NULL);
    CHECK(errno == EINVAL);
    CHECK((unsigned char)buf[0] == 0x5a);
}

/* 22: copy after free is refused as use-after-free (EFAULT). */
static void sc_05_copy_after_free(void)
{
    char *p = malloc(16);
    char *r;
    if (p == NULL) {
        CHECK(0);
        return;
    }
    errno = 0;
    r = strcpy(p, "abc");
    CHECK(r == p);
    free(p);
    errno = 0;
    r = strcpy(p, "x");
    CHECK(r == NULL);
    CHECK(errno == EFAULT);
}

/* 23: copy after a shrinking realloc: the new block has the smaller
 * tracked capacity, so an over-long copy is refused. */
static void sc_05_copy_after_realloc_shrink(void)
{
    char *p = malloc(32);
    char *q;
    char *r;
    if (p == NULL) {
        CHECK(0);
        return;
    }
    errno = 0;
    r = strcpy(p, "hello");
    CHECK(r == p);
    q = realloc(p, 8);
    CHECK(q != NULL);
    if (q == NULL) return;
    CHECK(cplus_safe_ptr_size(q) == 8);
    fill_bytes((unsigned char *)q, 8, 0x5a);
    errno = 0;
    r = strcpy(q, "abcdefghij");
    CHECK(r == NULL);
    CHECK(errno == ERANGE);
    CHECK((unsigned char)q[0] == 0);
    free(q);
}

/* 24: strict unknown-capacity mode. After the fix, string wrappers honour
 * the strict policy exactly like the memory wrappers: a destination whose
 * capacity cannot be proven is refused instead of being copied blindly. */
static void sc_05_strict_unknown_not_refused(void)
{
    char *r;
    void *m;
    cplus_safe_set_unknown_capacity_mode(1);
    fill_bytes((unsigned char *)g_host, g_host_size, 0x5a);
    errno = 0;
    r = strcpy(g_host, "xy");
    CHECK(r == NULL);
    CHECK(errno == EINVAL);
    CHECK((unsigned char)g_host[0] == 0x5a); /* untouched on refusal */

    fill_bytes((unsigned char *)g_host, g_host_size, 0x5a);
    errno = 0;
    r = strncpy(g_host, "xy", 2);
    CHECK(r == NULL);
    CHECK(errno == EINVAL);
    CHECK((unsigned char)g_host[0] == 0x5a);

    errno = 0;
    m = cplus_memcpy_s_auto(g_host, "Q", 1, (size_t)-1);
    CHECK(m == NULL);
    CHECK(errno == EINVAL);
    cplus_safe_set_unknown_capacity_mode(0);
}

/* 25: logging/statistics parity - one success and one refusal are recorded
 * for each of the two checked entry points. */
static void sc_05_logging_parity(void)
{
    cplus_safe_log_level old = cplus_safe_log_get_level();
    cplus_safe_stat st;
    struct { char buf[8]; } s;
    char *r;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE);

    cplus_safe_stats_reset();
    fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
    errno = 0;
    r = strcpy(s.buf, "ok");
    CHECK(r == s.buf);
    errno = 0;
    r = strcpy(s.buf, "12345678");
    CHECK(r == NULL);
    CHECK(cplus_safe_stats_get("strcpy_s", &st) == 1);
    CHECK(st.calls == 2);
    CHECK(st.ok == 1);
    CHECK(st.fail == 1);

    cplus_safe_stats_reset();
    fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
    errno = 0;
    r = strncpy(s.buf, "ok", 2);
    CHECK(r == s.buf);
    errno = 0;
    r = strncpy(s.buf, "abcdefgh", 8);
    CHECK(r == NULL);
    CHECK(cplus_safe_stats_get("strncpy_s", &st) == 1);
    CHECK(st.calls == 2);
    CHECK(st.ok == 1);
    CHECK(st.fail == 1);

    cplus_safe_log_set_level(old);
}

/* 26: repeated copies into the same buffer stay bounded every time. */
static void sc_05_repeated_copies_loop(void)
{
    static const char *const words[] = {
        "a", "bb", "ccc", "dddd", "eeeee", "ffffff", "ggggggg", ""
    };
    struct { char buf[16]; char post[4]; } s;
    size_t i;
    for (i = 0; i < 64; ++i) {
        const char *w = words[i % 8];
        char *r;
        fill_bytes((unsigned char *)&s, sizeof s, 0x5a);
        errno = 0;
        r = strcpy(s.buf, w);
        CHECK(r == s.buf);
        CHECK(errno == 0);
        CHECK(strcmp(s.buf, w) == 0);
        CHECK(s.buf[strlen(w)] == '\0');
        CHECK(canary_ok((const unsigned char *)s.post, sizeof s.post, 0x5a));
    }
}

int main(void)
{
    cplus_runtime_set_failstop(0);
    cplus_runtime_set_violation_trap(0);
    CHECK(cplus_runtime_get_violation_trap() == 0);

    /* Raw buffer first: no tracked block has been freed at this point. */
    sc_05_host_malloc_unknown();

    sc_05_exact_fit();
    sc_05_one_too_long_refused();
    sc_05_empty_string();
    sc_05_embedded_nul();
    sc_05_unterminated_bounded_alternative();
    sc_05_null_dest();
    sc_05_null_src();
    sc_05_stack_dest_canaries();
    sc_05_global_dest();
    sc_05_struct_member_canary();
    sc_05_heap_tracked_dest();
    sc_05_dest_one_past_end();
    sc_05_self_copy();
    sc_05_overlap_refused();
    sc_05_very_long_source();
    sc_05_strncpy_short_n();
    sc_05_strncpy_n_equals_cap();
    sc_05_strncpy_n_above_cap();
    sc_05_strncpy_zero_n();
    sc_05_strncpy_null_args();
    sc_05_copy_after_free();
    sc_05_copy_after_realloc_shrink();
    sc_05_strict_unknown_not_refused();
    sc_05_logging_parity();
    sc_05_repeated_copies_loop();

    host_release(g_host);
    g_host = NULL;

    if (g_failures != 0) {
        fprintf(stderr, "runtime_strcpy: %d/%d checks failed\n",
                g_failures, g_checks);
        return 1;
    }
    printf("runtime_strcpy: %d scenarios ok\n", SCENARIO_COUNT);
    return 0;
}
