/*
 * #22 contract-violation corpus.
 *
 * These cases are DEFINED BEHAVIOR only: they model invalid/error conditions
 * that libc reports via return values/errno (NULL, EOF, short count, truncation).
 *
 * UNDEFINED BEHAVIOR cases (UAF, double free, OOB writes, overlapping memcpy,
 * fclose twice, invalid pointer deref) are listed in docs/CRT_MATRIX.md as
 * "EXPLICIT UNSAFE BOUNDARY / compiler-rejected in safe mode" and are NOT
 * executed here because executing UB cannot be a reliable test.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static void check(int cond, const char *name) { (void)name; if (!cond) failures++; }

int main(void) {
    /* fopen invalid mode/path -> NULL + errno */
    errno = 0;
    FILE *f = fopen("/no/such/cplus-crt-path", "rb");
    check(f == NULL, "fopen missing returns NULL");
    check(errno != 0, "fopen missing sets errno");

    /* fread/fwrite on read-only/write-only mismatch not executed as UB;
       instead test EOF and ferror contracts on valid streams. */
    f = tmpfile();
    check(f != NULL, "tmpfile for EOF");
    if (f) {
        char b[4];
        size_t r = fread(b, 1, sizeof(b), f);
        check(r == 0, "fread at EOF returns 0");
        check(feof(f) != 0, "feof after EOF");
        check(ferror(f) == 0, "ferror clear at EOF");
        fclose(f);
    }

    /* failed realloc: old owner remains valid */
    void *p = malloc(64);
    check(p != NULL, "malloc for realloc fail");
    if (p) {
        memset(p, 0x5A, 64);
        void *q = realloc(p, (size_t)-1);
        check(q == NULL, "realloc huge fails");
        check(((unsigned char *)p)[0] == 0x5A, "realloc fail preserves owner bytes");
        free(p);
    }

    /* strcpy/strcat with insufficient destination is UB: DO NOT EXECUTE.
       Safe contract: callers must prove capacity; runtime check belongs to
       compiler/fortify/object-size. Classification recorded in matrix. */

    /* snprintf reports required size when buffer too small */
    char small[4];
    int need = snprintf(small, sizeof(small), "toolong");
    check(need == 7, "snprintf reports required size");
    check(small[3] == '\0', "snprintf NUL terminates");

    /* scanf format mismatch leaves defined stream state */
    {
        FILE *s = tmpfile();
        int v = 123;
        check(s != NULL, "tmpfile scanf mismatch");
        if (s) {
            fputs("xyz", s);
            rewind(s);
            int rc = fscanf(s, "%d", &v);
            check(rc == 0, "scanf mismatch returns 0");
            /* v may be indeterminate on mismatch in C; don't assert value. */
            clearerr(s);
            char word[8] = {0};
            rc = fscanf(s, "%7s", word);
            check(rc == 1, "scanf can continue after mismatch/clearerr");
            check(strcmp(word, "xyz") == 0, "scanf reads remaining token");
            fclose(s);
        }
    }

    printf("contract_violations: %s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
