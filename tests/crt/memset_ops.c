/* CRT conformance: memset defined behavior. */
#include <stdio.h>
#include <string.h>

static int failures;
static void check(int cond, const char *name) { (void)name; if (!cond) failures++; }

int main(void) {
    unsigned char b[16];
    memset(b, 0xAB, sizeof(b));
    check(b[0] == 0xAB && b[15] == 0xAB, "memset full");
    memset(b + 4, 0, 4);
    check(b[3] == 0xAB && b[4] == 0 && b[7] == 0 && b[8] == 0xAB, "memset range");
    memset(b, 1, 0); /* zero count no-op */
    check(b[4] == 0, "memset zero count");
    printf("memset_ops: %s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
