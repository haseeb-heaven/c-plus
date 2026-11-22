/* Direct-call coverage for the three previously-untested exported symbols
 * flagged in docs/libc/SYMBOL_AUDIT.md:
 *   cplus_putenv, cplus_vprintf, cplus_vfscanf.
 *
 * Each is called with defined inputs only. cplus_vprintf / cplus_vfscanf take
 * a va_list, so small variadic wrapper helpers build the va_list for us. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../../libc/install-include/cplus_libc.h"

/* va_list wrapper helper: forward variadic args to cplus_vprintf. */
static int do_vprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = cplus_vprintf(fmt, ap);
    va_end(ap);
    return r;
}

/* va_list wrapper helper: forward variadic args to cplus_vfscanf. */
static int do_vfscanf(FILE *fp, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = cplus_vfscanf(fp, fmt, ap);
    va_end(ap);
    return r;
}

/* putenv() keeps the caller's pointer, so the strings must stay alive. */
static char env_one[] = "CPLUS_COV_UNKNOWN=one";
static char env_two[] = "CPLUS_COV_UNKNOWN=two";

int main(void) {
    /* ---- cplus_vprintf: returns the number of characters written ---- */
    int n1 = do_vprintf("cov vprintf %d %s\n", 7, "x");
    assert(n1 == (int)strlen("cov vprintf 7 x\n"));   /* exact byte count */
    int n2 = do_vprintf("%d", 12345);
    assert(n2 == 5);                                   /* exact byte count */
    int n3 = do_vprintf("%s", "");                     /* empty output */
    assert(n3 == 0);

    /* ---- cplus_putenv: set, override, read back via getenv ---- */
    assert(cplus_putenv(env_one) == 0);
    char *v = cplus_getenv("CPLUS_COV_UNKNOWN");
    assert(v != NULL);
    assert(strcmp(v, "one") == 0);

    assert(cplus_putenv(env_two) == 0);                /* override same name */
    v = cplus_getenv("CPLUS_COV_UNKNOWN");
    assert(v != NULL);
    assert(strcmp(v, "two") == 0);

    assert(cplus_unsetenv("CPLUS_COV_UNKNOWN") == 0);
    assert(cplus_getenv("CPLUS_COV_UNKNOWN") == NULL);

    /* ---- cplus_vfscanf: parse defined input from a file ---- */
    const char *path = "/tmp/cplus_cov_vfscanf.tmp";
    (void)cplus_remove(path);
    FILE *f = cplus_fopen(path, "wb+");
    assert(f != NULL);
    const char *payload = "42 hello\n-7\n";
    assert(cplus_fwrite(payload, 1, strlen(payload), f) == strlen(payload));
    assert(cplus_fseek(f, 0L, SEEK_SET) == 0);

    int num = 0;
    char word[16];
    int got = do_vfscanf(f, "%d %15s", &num, word);
    assert(got == 2);
    assert(num == 42);
    assert(strcmp(word, "hello") == 0);

    int neg = 0;
    assert(do_vfscanf(f, "%d", &neg) == 1);
    assert(neg == -7);

    assert(cplus_fclose(f) == 0);
    assert(cplus_remove(path) == 0);

    puts("cplus_putenv: ok");
    puts("cplus_vprintf: ok");
    puts("cplus_vfscanf: ok");
    puts("test_coverage_untested: ok");
    return 0;
}
