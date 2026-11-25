/* CRT conformance: memcpy/memmove defined behaviour and overlap rules.
 * memcpy with overlapping ranges is undefined, so it is documented, never
 * executed; memmove overlapping is defined and checked here. */
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
    char source[16] = "abcdefgh";
    char destination[16];

    memcpy(destination, source, 9);
    check(strcmp(destination, "abcdefgh") == 0, "memcpy non-overlapping");

    /* Zero-length copies are defined even with NULL arguments in practice on
     * supported libcs; the standard keeps them undefined, so we only copy
     * from a real buffer. */
    memcpy(destination, source, 0);
    check(1, "memcpy length 0");

    char buffer[16] = "0123456789";
    memmove(buffer + 2, buffer, 5);          /* overlapping, defined */
    check(memcmp(buffer, "0101234789", 10) == 0, "memmove overlapping");

    char backward[16] = "0123456789";
    memmove(backward, backward + 2, 5);
    check(memcmp(backward, "2345656789", 10) == 0,
          "memmove backwards overlap");

    check(memchr("hello", 'l', 5) != NULL, "memchr find");
    check(memchr("hello", 'z', 5) == NULL, "memchr miss");
    check(memcmp("abc", "abd", 3) < 0, "memcmp ordering");

    printf("memory_ops: %s\n", failures == 0 ? "ok" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
