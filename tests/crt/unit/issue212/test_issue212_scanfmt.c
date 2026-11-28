/* White-box regression for issue #212.
 *
 * The vulnerable implementation rewrote the scanf format into a malloc'd
 * buffer sized flen * 2 + 64 while appending up to 10 bytes per bounded %s;
 * 14 %s specs plus %d overflowed that buffer by 19 bytes.  This branch instead
 * drops Annex K capacities after one va_list inspection and forwards the
 * original format, so there is no temporary format buffer at all.
 *
 * Compile safe_format.c into this TU, replace the host scanf entry points with
 * deterministic stubs, and assert: (a) no allocation is made by scan_dispatch,
 * (b) no format rewrite happens, and (c) the exact pointer words (not capacity
 * integers) reach the host in both the %c and the many-bounded-spec cases. */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Annex K capacities intentionally follow %s/%c pointers, which the host's
 * scanf format attribute does not describe. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wformat"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wformat"
#endif

void *test_malloc(size_t n);
void test_free(void *p);
static int test_sscanf(const char *str, const char *fmt, ...);
static int test_vsscanf(const char *str, const char *fmt, va_list ap);

#define CPLUS_SAFE_DIAG_NO_MACROS
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS
#define malloc test_malloc
#define free test_free
#define sscanf test_sscanf
#define vsscanf test_vsscanf
#include "../../../stdlib/safe/src/safe_format.c"
#undef malloc
#undef free
#undef sscanf
#undef vsscanf

#define MAX_ARGS 16u

static size_t g_alloc_calls;
static size_t g_free_calls;
static int g_violations;
static int g_host_calls;
static int g_host_ret;
static char g_seen_fmt[512];
static void *g_seen_args[MAX_ARGS];

void cplus_safe_log(cplus_safe_log_level level, const char *api, const char *msg) {
    (void)level; (void)api; (void)msg;
}
void cplus_safe_stat_record(const char *api, int ok) { (void)api; (void)ok; }
void cplus_runtime_violation(const char *api, const char *detail) {
    (void)api; (void)detail; ++g_violations;
}
int cplus_safe_ptr_was_freed(const void *ptr) { (void)ptr; return 0; }
size_t cplus_safe_ptr_size(const void *ptr) { (void)ptr; return 0; }
size_t cplus_safe_remaining_capacity(const void *ptr) { (void)ptr; return (size_t)-1; }
int cplus_io_stream_is_closed(const FILE *stream) { (void)stream; return 0; }

void *test_malloc(size_t n) {
    ++g_alloc_calls;
    return malloc(n);
}
void test_free(void *p) {
    ++g_free_calls;
    free(p);
}

static void capture(const char *fmt, va_list ap) {
    size_t i;
    snprintf(g_seen_fmt, sizeof(g_seen_fmt), "%s", fmt);
    for (i = 0; i < MAX_ARGS; ++i) g_seen_args[i] = va_arg(ap, void *);
    ++g_host_calls;
}

static int test_sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    (void)str;
    va_start(ap, fmt);
    capture(fmt, ap);
    va_end(ap);
    return g_host_ret;
}
static int test_vsscanf(const char *str, const char *fmt, va_list ap) {
    (void)str;
    capture(fmt, ap);
    return g_host_ret;
}

static void reset(void) {
    size_t i;
    g_alloc_calls = 0;
    g_free_calls = 0;
    g_violations = 0;
    g_host_calls = 0;
    g_host_ret = 0;
    g_seen_fmt[0] = '\0';
    for (i = 0; i < MAX_ARGS; ++i) g_seen_args[i] = NULL;
    errno = 0;
}

int main(void) {
    char buf[16][2];
    char ch = 0, a = 0, b = 0, word[8];
    int val = 0;
    size_t i;

    memset(buf, 0, sizeof(buf));
    memset(word, 0, sizeof(word));

    /* Exact issue #212 reproducer: %c is bounded, so the (rsize_t)1 capacity
     * is consumed here instead of becoming the %d pointer (address 0x1). */
    reset();
    assert(cplus_sscanf_s("a 42", "%c %d", &ch, (size_t)1, &val) == 0);
    assert(g_violations == 0 && g_host_calls == 1);
    assert(g_alloc_calls == 0 && g_free_calls == 0);
    assert(strcmp(g_seen_fmt, "%c %d") == 0);
    assert(g_seen_args[0] == (void *)&ch && g_seen_args[1] == (void *)&val);

    /* The old allocation was 124 bytes for 143 bytes of rewritten format.
     * Fifteen bounded conversions must not allocate or rewrite anything. */
    reset();
    assert(cplus_sscanf_s("a b c d e f g h i j k l m n 42",
        "%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s %d",
        buf[0], (size_t)2, buf[1], (size_t)2, buf[2], (size_t)2,
        buf[3], (size_t)2, buf[4], (size_t)2, buf[5], (size_t)2,
        buf[6], (size_t)2, buf[7], (size_t)2, buf[8], (size_t)2,
        buf[9], (size_t)2, buf[10], (size_t)2, buf[11], (size_t)2,
        buf[12], (size_t)2, buf[13], (size_t)2, &val) == 0);
    assert(g_violations == 0 && g_host_calls == 1);
    assert(g_alloc_calls == 0 && g_free_calls == 0);
    assert(strcmp(g_seen_fmt, "%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s%1s %d") == 0);
    for (i = 0; i < 14u; ++i) assert(g_seen_args[i] == (void *)&buf[i]);
    assert(g_seen_args[14] == (void *)&val);

    /* Mixed %c/%s capacities all land in the right argument slots. */
    reset();
    assert(cplus_sscanf_s("xy abc 7", "%c%c %3s %d",
        &a, (size_t)1, &b, (size_t)1, word, (size_t)sizeof(word), &val) == 0);
    assert(g_violations == 0 && g_host_calls == 1);
    assert(g_alloc_calls == 0);
    assert(strcmp(g_seen_fmt, "%c%c %3s %d") == 0);
    assert(g_seen_args[0] == (void *)&a && g_seen_args[1] == (void *)&b);
    assert(g_seen_args[2] == (void *)word && g_seen_args[3] == (void *)&val);

    /* A zero capacity word is never forwarded as a destination pointer. */
    reset();
    assert(cplus_sscanf_s("a 42", "%c %d", &ch, (size_t)0, &val) == -1);
    assert(errno == EINVAL && g_violations > 0 && g_host_calls == 0);
    assert(g_alloc_calls == 0);

    /* An unbounded string with a capacity would need a width the host format
     * does not have; refuse it instead of writing past the caller's buffer. */
    reset();
    assert(cplus_sscanf_s("abc 42", "%s %d", word, (size_t)sizeof(word), &val) == -1);
    assert(errno == EINVAL && g_violations > 0 && g_host_calls == 0);

    /* Capacity smaller than an explicit field width is still refused. */
    reset();
    assert(cplus_sscanf_s("abcdef 42", "%5s %d", word, (size_t)3, &val) == -1);
    assert(errno == EINVAL && g_violations > 0 && g_host_calls == 0);

    puts("issue212_scanfmt: ok");
    return 0;
}
