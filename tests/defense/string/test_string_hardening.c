/* Defense-in-depth regression tests for the checked string adapters.
 *
 * Each case is run in its own process (see run.sh) so that a pre-fix
 * unbounded read is reported as a crash (SIGSEGV) rather than taking down
 * the whole suite.  Cases cover the reviewed gap list: null args,
 * zero-length defined behavior, one-byte-too-small dest, unterminated source
 * and destination (bounded scan via a PROT_NONE guard page), overlap /
 * self-append, SIZE_MAX smax, truncation, positive error codes, and the
 * safeclib constraint handler.
 *
 * The guard-page cases are the exploit reproductions: before hardening,
 * cplus_str{cpy,cat}_s_auto / cplus_strncat_s_auto call strlen() on a buffer
 * whose last byte is flush against an unmapped page, so the unbounded scan
 * faults.  After hardening the scan is bounded by the destination capacity
 * and the operation is REFUSED with ERANGE (errno) / NULL (return value).
 */
#include "safe_string.h"
#include "safe_runtime.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Exported by third_party/safeclib/src/cplus_sc_wrappers.c (not a C+ header). */
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*)(const char *, void *, int));

static int g_handler_calls = 0;
static void counting_handler(const char *msg, void *ptr, int err) {
    (void)msg; (void)ptr; (void)err;
    ++g_handler_calls;
}

#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);      \
            return 1;                                                         \
        }                                                                     \
    } while (0)

/* Allocate an N-byte buffer whose last byte is immediately before a PROT_NONE
 * guard page and fill it with non-NUL bytes (a deterministic unterminated
 * string).  The mapping is intentionally leaked: each case runs in its own
 * short-lived process. */
static char *guard_buffer(size_t n) {
    long ps = sysconf(_SC_PAGESIZE);
    char *region;
    if (ps <= 0 || n > (size_t)ps) return NULL;
    region = (char *)mmap(NULL, (size_t)ps * 2u, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) return NULL;
    if (mprotect(region + ps, (size_t)ps, PROT_NONE) != 0) {
        munmap(region, (size_t)ps * 2u);
        return NULL;
    }
    memset(region + ps - n, 'A', n);
    return region + ps - n;
}

static int case_null_args(void) {
    char buf[16] = "xyz";
    CHECK(cplus_strcpy_s(NULL, 8, "x") == 400);
    CHECK(cplus_strcpy_s(buf, 8, NULL) == 400);
    CHECK(cplus_strncpy_s(NULL, 8, "x", 1) == 400);
    CHECK(cplus_strncpy_s(buf, 8, NULL, 1) == 400);
    CHECK(cplus_strcat_s(buf, 8, NULL) == 400);
    CHECK(cplus_strncat_s(NULL, 8, "x", 1) == 400);
    CHECK(cplus_strcmp_s(buf, 8, "x", NULL) == 400);
    CHECK(cplus_strnlen_s(NULL, 4) == 0);
    errno = 0;
    CHECK(cplus_strcpy_s_auto(NULL, "x", 8) == NULL && errno == EINVAL);
    errno = 0;
    CHECK(cplus_strcpy_s_auto(buf, NULL, 8) == NULL && errno == EINVAL);
    CHECK(cplus_strcat_s_auto(buf, NULL, 8) == NULL);
    CHECK(cplus_strncat_s_auto(buf, NULL, 1, 8) == NULL);
    return 0;
}

static int case_zero_length(void) {
    char d[8] = "zzzzzzz";
    CHECK(cplus_strncpy_s(d, sizeof d, "abc", 0) == 0);
    CHECK(d[0] == '\0');
    strcpy(d, "ab");
    CHECK(cplus_strncat_s_auto(d, "cd", 0, sizeof d) == d);
    CHECK(strcmp(d, "ab") == 0);
    strcpy(d, "zz");
    CHECK(cplus_strncpy_s_auto(d, "abc", 0, sizeof d) == d);
    CHECK(strcmp(d, "zz") == 0);
    CHECK(cplus_strnlen_s("abc", 0) == 0);
    CHECK(cplus_strcat_s(d, sizeof d, "") == 0);
    return 0;
}

static int case_one_small(void) {
    char d[3];
    CHECK(cplus_strcpy_s(d, 3, "abc") == 406); /* one byte too small */
    char e[4] = "XXXX";
    CHECK(cplus_strcpy_s(e, 3, "ab") == 0);
    CHECK(strcmp(e, "ab") == 0);
    char f[3];
    errno = 0;
    CHECK(cplus_strcpy_s_auto(f, "abc", 3) == NULL && errno == ERANGE);
    char g[4] = "ab";
    errno = 0;
    CHECK(cplus_strcat_s_auto(g, "cde", 4) == NULL && errno == ERANGE);
    return 0;
}

static int case_overlap(void) {
    char b[16] = "abcdef";
    CHECK(cplus_strcpy_s(b, 16, b + 1) == 404);
    char c[16] = "ab";
    CHECK(cplus_strcat_s(c, 16, c) == 404);
    char d[8] = "ab";
    CHECK(cplus_strcat_s_auto(d, d, 8) == NULL); /* self-append refused */
    return 0;
}

static int case_sizemax_smax(void) {
    CHECK(cplus_strnlen_s("abc", SIZE_MAX) == 0);          /* defined refusal */
    CHECK(cplus_strnlen_s("abc", RSIZE_MAX_STR + 1u) == 0); /* defined refusal */
    CHECK(cplus_strnlen_s("abc", 3) == 3);
    CHECK(cplus_strnlen_s("abc", 4) == 3);
    return 0;
}

static int case_truncation(void) {
    char d[4];
    CHECK(cplus_strncpy_s(d, 4, "abcdefgh", 3) == 0);
    CHECK(memcmp(d, "abc\0", 4) == 0);
    char e[4] = "ab";
    CHECK(cplus_strncat_s(e, 4, "cdef", 1) == 0); /* dlen 2 + 1 + NUL == 4 */
    CHECK(strcmp(e, "abc") == 0);
    char f[3];
    CHECK(cplus_strncpy_s(f, 3, "abcd", 3) == 406); /* no room for NUL */
    return 0;
}

static int case_codes(void) {
    char d[2];
    errno_t e1 = cplus_strcpy_s(NULL, 2, "x");
    errno_t e2 = cplus_strcpy_s(d, 0, "x");
    errno_t e3 = cplus_strcpy_s(d, 2, "x");
    CHECK(e1 != 0 && e1 > 0);
    CHECK(e2 != 0 && e2 > 0);
    CHECK(e1 != e2);
    CHECK(e3 == 0);
    return 0;
}

static int case_handler(void) {
    char d[4];
    cplus_sc_set_str_constraint_handler_s_wrapper(counting_handler);
    g_handler_calls = 0;
    CHECK(cplus_strcpy_s(d, 0, "x") == 401); /* ESZEROL -> safeclib handler */
    CHECK(g_handler_calls > 0);
    cplus_sc_set_str_constraint_handler_s_wrapper(NULL); /* restore ignore */
    return 0;
}

/* ---- exploit reproductions: unterminated buffer against a guard page ---- */

static int case_unterminated_src_strcpy(void) {
    enum { N = 32 };
    char dest[N];
    char *src = guard_buffer(N);
    if (!src) return 2;
    errno = 0;
    CHECK(cplus_strcpy_s_auto(dest, src, N) == NULL);
    CHECK(errno == ERANGE);
    return 0;
}

static int case_unterminated_src_strcat(void) {
    enum { N = 32 };
    char dest[N] = "d";
    char *src = guard_buffer(N);
    if (!src) return 2;
    errno = 0;
    CHECK(cplus_strcat_s_auto(dest, src, N) == NULL);
    CHECK(errno == ERANGE);
    return 0;
}

static int case_unterminated_dest_strcat(void) {
    enum { N = 32 };
    char *dest = guard_buffer(N);
    char src[8] = "ok";
    if (!dest) return 2;
    errno = 0;
    CHECK(cplus_strcat_s_auto(dest, src, N) == NULL);
    CHECK(errno == ERANGE);
    return 0;
}

static int case_unterminated_dest_strncat(void) {
    enum { N = 32 };
    char *dest = guard_buffer(N);
    char src[8] = "ok";
    if (!dest) return 2;
    errno = 0;
    CHECK(cplus_strncat_s_auto(dest, src, 1, N) == NULL);
    CHECK(errno == ERANGE);
    return 0;
}

static int case_auto_macro_valid(void) {
    /* Valid-input semantics of the 2-arg standard-C macros must not change. */
    char dest[8];
    CHECK(strcpy_s(dest, "hello") == dest);
    CHECK(strcmp(dest, "hello") == 0);
    CHECK(strcat_s(dest, "!") == dest);
    CHECK(strcmp(dest, "hello!") == 0);
    char nbuf[8];
    CHECK(strncpy_s(nbuf, "abcdef", 3) == nbuf);
    CHECK(memcmp(nbuf, "abc\0", 4) == 0);
    return 0;
}

struct test_case { const char *name; int (*fn)(void); };
static const struct test_case CASES[] = {
    {"null_args", case_null_args},
    {"zero_length", case_zero_length},
    {"one_small", case_one_small},
    {"overlap", case_overlap},
    {"sizemax_smax", case_sizemax_smax},
    {"truncation", case_truncation},
    {"codes", case_codes},
    {"handler", case_handler},
    {"unterminated_src_strcpy", case_unterminated_src_strcpy},
    {"unterminated_src_strcat", case_unterminated_src_strcat},
    {"unterminated_dest_strcat", case_unterminated_dest_strcat},
    {"unterminated_dest_strncat", case_unterminated_dest_strncat},
    {"auto_macro_valid", case_auto_macro_valid},
};

int main(int argc, char **argv) {
    size_t i;
    cplus_runtime_set_violation_trap(0); /* observe NULL/ERANGE, don't exit(70) */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    for (i = 0; i < sizeof CASES / sizeof CASES[0]; ++i) {
        if (strcmp(argv[1], CASES[i].name) == 0) return CASES[i].fn();
    }
    fprintf(stderr, "unknown case: %s\n", argv[1]);
    return 2;
}
