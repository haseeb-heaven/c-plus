/*
 * lc32 - C library interop: stdio (snprintf / sscanf / string streams).
 *
 * Independent legacy-C ABI probe: formatting and parsing through libc calls,
 * checking return values exactly as a C caller would.
 */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[64];
    char name[16];
    int n, id, score, consumed;

    n = snprintf(buf, sizeof(buf), "%s:%d/%d", "lc32", 32, 97);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        printf("lc32 result: FAIL snprintf n=%d\n", n);
        return 1;
    }
    if (strcmp(buf, "lc32:32/97") != 0) {
        printf("lc32 result: FAIL formatted '%s'\n", buf);
        return 1;
    }
    consumed = sscanf(buf, "%15[^:]:%d/%d", name, &id, &score);
    if (consumed != 3 || strcmp(name, "lc32") != 0 || id != 32 ||
        score != 97) {
        printf("lc32 result: FAIL parse n=%d name=%s id=%d score=%d\n",
               consumed, name, id, score);
        return 1;
    }
    printf("lc32 result: OK text='%s' name=%s id=%d score=%d\n",
           buf, name, id, score);
    return 0;
}
