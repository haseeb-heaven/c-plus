/* CRT conformance: printf-family defined behavior via snprintf/fprintf.
 * Direct stdout printf is avoided for deterministic diff; snprintf/fprintf(tmpfile)
 * exercise the same formatting contracts. */
#include <stdio.h>
#include <string.h>

static int failures;
static void check(int cond, const char *name) { (void)name; if (!cond) failures++; }

int main(void) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%s %d %.2f", "hi", 7, 3.5);
    check(n > 0, "snprintf positive length");
    check(strcmp(buf, "hi 7 3.50") == 0, "snprintf formatted");
    n = snprintf(buf, 4, "%s", "abcdef");
    check(n == 6, "snprintf truncation length");
    check(strcmp(buf, "abc") == 0, "snprintf truncation NUL");

    FILE *f = tmpfile();
    check(f != NULL, "tmpfile");
    if (f) {
        int p = fprintf(f, "x=%d", 42);
        check(p == 4, "fprintf length");
        rewind(f);
        char got[16] = {0};
        check(fread(got, 1, sizeof(got)-1, f) == 4, "fread fprintf output");
        check(strcmp(got, "x=42") == 0, "fprintf content");
        fclose(f);
    }
    printf("printf_ops: %s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
