/* Deterministic regressions for issue #197: bounded string functions must
 * resolve capacity before scanning (no unbounded cplus_strlen on an
 * unterminated heap block), and cplus_strtok must validate delim and keep its
 * continuation state per thread instead of in the host's global strtok state.
 *
 * Output: "bounded_strtok_attack: <checks> <failures>".
 */
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "cplus_libc.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) { ++g_failures; fprintf(stderr, "FAIL: %s\n", (name)); }  \
    } while (0)

#define CHECK_EQ(actual, expected, name)                                       \
    do {                                                                       \
        int a_ = (int)(actual);                                                \
        int e_ = (int)(expected);                                              \
        ++g_checks;                                                            \
        if (a_ != e_) {                                                        \
            ++g_failures;                                                      \
            fprintf(stderr, "FAIL: %s (got %d, expected %d)\n", (name), a_, e_); \
        }                                                                      \
    } while (0)

static int unchanged(const void *p, unsigned char v, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; ++i)
        if (b[i] != v) return 0;
    return 1;
}

static void test_unterminated_destination(void)
{
    char *d = (char *)cplus_malloc(16);
    CHECK(d != NULL, "unterminated dest allocation");
    if (d == NULL) return;
    memset(d, 'A', 16); /* no NUL anywhere in the tracked block */

    errno = 0;
    CHECK(cplus_strcat_bounded(d, "x", (size_t)-1) == NULL,
          "strcat refuses unterminated destination");
    CHECK(errno == ERANGE, "strcat dest refusal is ERANGE");
    CHECK(unchanged(d, 'A', 16), "strcat refusal leaves dest intact");

    errno = 0;
    CHECK(cplus_strncat_bounded(d, "x", 1, (size_t)-1) == NULL,
          "strncat refuses unterminated destination");
    CHECK(errno == ERANGE, "strncat dest refusal is ERANGE");
    CHECK(unchanged(d, 'A', 16), "strncat refusal leaves dest intact");

    /* A properly terminated destination of the same size still works. */
    d[0] = '\0';
    errno = 0;
    CHECK(cplus_strcat_bounded(d, "ok", (size_t)-1) == d,
          "terminated dest accepts strcat");
    CHECK(errno == 0 && strcmp(d, "ok") == 0, "strcat result");
    cplus_free(d);
}

static void test_unterminated_source(void)
{
    char *src = (char *)cplus_malloc(8);
    char *d = (char *)cplus_malloc(32);
    CHECK(src != NULL && d != NULL, "unterminated source allocations");
    if (src == NULL || d == NULL) { cplus_free(src); cplus_free(d); return; }
    memset(src, 'B', 8); /* tracked block with no terminator */
    memset(d, 0x5A, 32);

    errno = 0;
    CHECK(cplus_strcpy_bounded(d, src, (size_t)-1) == NULL,
          "strcpy refuses unterminated source within capacity");
    CHECK(errno == ERANGE, "strcpy source refusal is ERANGE");
    CHECK(unchanged(d, 0x5A, 32), "strcpy refusal leaves dest intact");

    errno = 0;
    CHECK(cplus_strcat_bounded(d, src, (size_t)-1) == NULL,
          "strcat refuses unterminated source");
    CHECK(errno == ERANGE, "strcat source refusal is ERANGE");

    /* Terminating within the tracked source makes the same call safe. */
    src[7] = '\0';
    d[0] = '\0';
    errno = 0;
    CHECK(cplus_strcpy_bounded(d, src, (size_t)-1) == d,
          "terminated tracked source accepted");
    CHECK(errno == 0 && strlen(d) == 7, "bounded strcpy length");
    cplus_free(src);
    cplus_free(d);
}

static void test_delim_null(void)
{
    char buf[] = "a,b,c";
    errno = 0;
    CHECK(cplus_strtok(buf, NULL) == NULL, "strtok refuses NULL delim");
    CHECK(errno == EINVAL, "strtok NULL delim sets EINVAL");
    errno = 0;
    CHECK(cplus_strtok(NULL, NULL) == NULL, "continuation with NULL delim refused");
    CHECK(errno == EINVAL, "continuation NULL delim sets EINVAL");
    /* No prior tokenisation in this thread: continuation must be safe. */
    CHECK(cplus_strtok(NULL, ",") == NULL, "no-state continuation returns NULL");
}

/* Two threads tokenising different buffers must not share hidden state. */
typedef struct tok_arg_s {
    const char *expected;
    int failures;
} tok_arg;

static volatile int g_start;

static void *tokenise(void *p)
{
    tok_arg *a = (tok_arg *)p;
    char buf[32];
    int i;
    while (g_start == 0) { /* spin until both workers are running */ }
    for (i = 0; i < 1000; ++i) {
        char *tok;
        int k;
        snprintf(buf, sizeof buf, "%s", a->expected);
        tok = cplus_strtok(buf, ",");
        for (k = 0; k < 3; ++k) {
            const char *want = a->expected + (k * 2);
            if (tok == NULL || tok[0] != want[0]) { ++a->failures; break; }
            tok = cplus_strtok(NULL, ",");
        }
        if (tok != NULL) ++a->failures;
    }
    return NULL;
}

static void test_thread_local_strtok(void)
{
    pthread_t t1, t2;
    tok_arg a1 = { "a,b,c", 0 };
    tok_arg a2 = { "x,y,z", 0 };

    g_start = 0;
    CHECK_EQ(pthread_create(&t1, NULL, tokenise, &a1), 0, "strtok thread 1 create");
    CHECK_EQ(pthread_create(&t2, NULL, tokenise, &a2), 0, "strtok thread 2 create");
    g_start = 1;
    CHECK_EQ(pthread_join(t1, NULL), 0, "strtok thread 1 join");
    CHECK_EQ(pthread_join(t2, NULL), 0, "strtok thread 2 join");
    CHECK_EQ(a1.failures, 0, "thread 1 tokens intact under interleaving");
    CHECK_EQ(a2.failures, 0, "thread 2 tokens intact under interleaving");
}

int main(void)
{
    test_unterminated_destination();
    test_unterminated_source();
    test_delim_null();
    test_thread_local_strtok();
    printf("bounded_strtok_attack: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
