/* White-box regression for issue #212.  Building safe_format.c in this TU
 * lets us instrument the temporary scanf format buffer: malloc gets a canary
 * pad, and the vsscanf stub checks it before inspecting the rewritten format
 * and the forwarded pointer arguments.  A single extra appended byte therefore
 * fails deterministically instead of relying on heap luck. */
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

static void *test_malloc(size_t n);
static void test_free(void *p);
static int test_vsscanf(const char *str, const char *fmt, va_list ap);

#define CPLUS_SAFE_DIAG_NO_MACROS
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS
#define malloc test_malloc
#define free test_free
#define vsscanf test_vsscanf
#include "../../../stdlib/safe/src/safe_format.c"
#undef malloc
#undef free
#undef vsscanf

#define CANARY_PAD 128u
#define CANARY_BYTE 0xA5u

static size_t g_alloc_req;
static size_t g_alloc_cap;
static unsigned char *g_alloc_base;
static int g_canary_ok;
static char g_seen_fmt[4096];
static void *g_arg0;
static void *g_arg1;
static int g_violations;

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

static void *test_malloc(size_t n) {
    unsigned char *p = (unsigned char *)malloc(n + CANARY_PAD);
    size_t i;
    g_alloc_req = n;
    g_alloc_cap = n;
    g_alloc_base = p;
    g_canary_ok = 1;
    if (!p) return NULL;
    for (i = 0; i < CANARY_PAD; ++i) p[n + i] = CANARY_BYTE;
    return p;
}

static void check_canary(void) {
    size_t i;
    if (!g_alloc_base) return;
    for (i = 0; i < CANARY_PAD; ++i)
        if (g_alloc_base[g_alloc_cap + i] != CANARY_BYTE) g_canary_ok = 0;
}

static void test_free(void *p) {
    check_canary();
    free(p);
    if (p == g_alloc_base) { g_alloc_base = NULL; g_alloc_cap = 0; }
}

static int test_vsscanf(const char *str, const char *fmt, va_list ap) {
    (void)str;
    g_arg0 = va_arg(ap, void *);
    g_arg1 = va_arg(ap, void *);
    check_canary();
    (void)snprintf(g_seen_fmt, sizeof(g_seen_fmt), "%s", fmt);
    return 0;
}

static size_t count_needle(const char *hay, const char *needle) {
    size_t n = 0;
    const char *p = hay;
    size_t len = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) { ++n; p += len; }
    return n;
}

static void reset(void) {
    g_alloc_req = 0;
    g_alloc_cap = 0;
    g_alloc_base = NULL;
    g_canary_ok = 1;
    g_seen_fmt[0] = '\0';
    g_arg0 = NULL;
    g_arg1 = NULL;
    g_violations = 0;
    errno = 0;
}

int main(void) {
    char buf[16][2];
    char ch = 0;
    int val = 0;
    char *big;
    size_t i;

    memset(buf, 0, sizeof(buf));

    /* 14 unbounded %s with the largest Annex K capacity + one %d.  A single
     * spec expands "%s" (2 bytes) into "%16777215s" (10 bytes); the old
     * flen*2+64 allocation was 124 bytes for 143 bytes of output. */
    reset();
    assert(cplus_sscanf_s("a b c d e f g h i j k l m n 42",
        "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%d",
        buf[0], (size_t)16777216, buf[1], (size_t)16777216,
        buf[2], (size_t)16777216, buf[3], (size_t)16777216,
        buf[4], (size_t)16777216, buf[5], (size_t)16777216,
        buf[6], (size_t)16777216, buf[7], (size_t)16777216,
        buf[8], (size_t)16777216, buf[9], (size_t)16777216,
        buf[10], (size_t)16777216, buf[11], (size_t)16777216,
        buf[12], (size_t)16777216, buf[13], (size_t)16777216,
        &val) == 0);
    assert(g_violations == 0);
    assert(g_canary_ok == 1);
    assert(g_alloc_req == 143u); /* 30 + 14*8 expansion bytes + NUL */
    assert(strlen(g_seen_fmt) == 142u);
    assert(count_needle(g_seen_fmt, "%16777215s") == 14u);
    assert(strcmp(g_seen_fmt + 140, "%d") == 0);

    /* 11 %s + %d overflowed the old allocation by exactly one byte. */
    reset();
    assert(cplus_sscanf_s("a b c d e f g h i j k 42",
        "%s%s%s%s%s%s%s%s%s%s%s%d",
        buf[0], (size_t)16777216, buf[1], (size_t)16777216,
        buf[2], (size_t)16777216, buf[3], (size_t)16777216,
        buf[4], (size_t)16777216, buf[5], (size_t)16777216,
        buf[6], (size_t)16777216, buf[7], (size_t)16777216,
        buf[8], (size_t)16777216, buf[9], (size_t)16777216,
        buf[10], (size_t)16777216, &val) == 0);
    assert(g_violations == 0 && g_canary_ok == 1);
    assert(g_alloc_req == 24u + 11u * 8u + 1u);
    assert(strlen(g_seen_fmt) == 24u + 11u * 8u);

    /* Annex K %c consumes its rsize_t capacity.  Before the fix the capacity
     * was forwarded as the %d pointer (address 0x1) and the next va_arg was
     * desynchronised. */
    reset();
    assert(cplus_sscanf_s("a 42", "%c %d", &ch, (size_t)1, &val) == 0);
    assert(g_violations == 0 && g_canary_ok == 1);
    assert(g_arg0 == (void *)&ch && g_arg1 == (void *)&val);
    assert(strcmp(g_seen_fmt, "%c %d") == 0);

    /* Explicit widths are narrowed to, never widened past, the capacity. */
    reset();
    assert(cplus_sscanf_s("abcdef", "%20s %d", buf[0], (size_t)16, &val) == 0);
    assert(g_violations == 0 && g_canary_ok == 1);
    assert(strcmp(g_seen_fmt, "%15s %d") == 0);

    reset();
    assert(cplus_sscanf_s("abcdef", "%5s %d", buf[0], (size_t)16, &val) == 0);
    assert(g_violations == 0 && g_canary_ok == 1);
    assert(strcmp(g_seen_fmt, "%5s %d") == 0);

    /* A width with more digits than the capacity's limit shrinks the format;
     * this exercises the checked subtractive branch of the size accumulator. */
    reset();
    assert(cplus_sscanf_s("abcdef", "%100s %d", buf[0], (size_t)10, &val) == 0);
    assert(g_violations == 0 && g_canary_ok == 1);
    assert(strcmp(g_seen_fmt, "%9s %d") == 0);
    assert(g_alloc_req == strlen("%9s %d") + 1u);

    /* Oversized formats are refused before any allocation is attempted. */
    i = ((size_t)1 << 20) + 64u;
    big = (char *)malloc(i);
    assert(big != NULL);
    big[0] = '%'; big[1] = 's'; big[2] = '%'; big[3] = 's'; big[4] = ' ';
    memset(big + 5, 'x', i - 6u);
    big[i - 1u] = '\0';
    reset();
    assert(cplus_sscanf_s("a b", big, buf[0], (size_t)8, buf[1], (size_t)8, &val) == -1);
    assert(errno == EINVAL && g_violations > 0 && g_alloc_req == 0u);
    free(big);

    puts("issue212_scanfmt_rewrite: ok");
    return 0;
}
