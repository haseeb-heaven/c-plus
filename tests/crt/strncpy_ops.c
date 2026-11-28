/* CRT conformance: strncpy/strncat defined behavior and known pitfalls.
 * strncpy does NOT guarantee NUL termination when source >= n. */
#include <stdio.h>
#include <string.h>

static int failures;
static void check(int cond, const char *name) { (void)name; if (!cond) failures++; }

int main(void) {
    char dst[8];
    memset(dst, 'X', sizeof(dst));
    strncpy(dst, "abc", sizeof(dst));
    check(dst[3] == '\0' && dst[7] == '\0', "strncpy pads with NULs");

    memset(dst, 'Y', sizeof(dst));
    strncpy(dst, "abcdefghi", 4); /* not NUL-terminated by strncpy */
    dst[4] = '\0'; /* explicit termination - required contract */
    check(memcmp(dst, "abcd", 4) == 0 && dst[4] == '\0', "strncpy truncation requires explicit NUL");

    char cat[16] = "ab";
    strncat(cat, "cdef", 2);
    check(strcmp(cat, "abcd") == 0, "strncat appends bounded");
    printf("strncpy_ops: %s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
