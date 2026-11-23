/* libc calls: string, stdio and stdlib entry points from C89. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    char buf[32];
    const char *s = "Hello";
    int n = strlen(s);

    strcpy(buf, s);
    strcat(buf, ",World");
    printf("%d %s %d\n", n, buf, (int)strlen(buf));
    printf("%d %d\n", (int)(strcmp("abc", "abc") == 0),
                      (int)(strcmp("abc", "abd") < 0));
    sprintf(buf, "%d-%d", 12, 34);
    printf("%s\n", buf);
    printf("%d\n", atoi("123") + abs(-7));
    return 0;
}
