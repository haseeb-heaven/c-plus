/* CRT conformance: strlen/strcpy/strcat and bounded alternatives.
 * Unbounded strcpy/strcat overflow is undefined, so it is documented rather
 * than executed; snprintf/strnlen are the defined, bounded paths. */
#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char *what) {
    if (!condition) {
        failures++;
        printf("FAIL %s\n", what);
    }
}

int main(void) {
    char buffer[16];

    check(strlen("") == 0, "strlen empty");
    check(strlen("hello") == 5, "strlen hello");
    check(strnlen("hello", 3) == 3, "strnlen bounded");
    check(strnlen("hi", 8) == 2, "strnlen short");

    memset(buffer, 0, sizeof(buffer));
    strcpy(buffer, "abc");
    check(strcmp(buffer, "abc") == 0, "strcpy fits");
    strcat(buffer, "def");
    check(strcmp(buffer, "abcdef") == 0, "strcat fits");

    /* Bounded copy that truncates is defined by C11 snprintf: it returns the
     * length that *would* have been written. */
    int needed = snprintf(buffer, 4, "%s", "abcdef");
    check(needed == 6, "snprintf return is the untruncated length");
    check(strcmp(buffer, "abc") == 0, "snprintf truncates and terminates");

    check(strchr("abc:def", ':') != NULL, "strchr hit");
    check(strrchr("a/b/c", '/') != NULL, "strrchr hit");
    check(strstr("haystack", "st") != NULL, "strstr hit");
    check(strncmp("abcdef", "abcxyz", 3) == 0, "strncmp prefix");

    printf("string_ops: %s\n", failures == 0 ? "ok" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
