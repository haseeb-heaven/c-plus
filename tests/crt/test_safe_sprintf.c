#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "safe_format.h"
int main(void) {
    char b[16];
    assert(cplus_sprintf_s(b, sizeof(b), "A%d", 1) == 2);
    assert(strcmp(b, "A1") == 0);
    char s[4];
    int n = cplus_sprintf_s(s, sizeof(s), "%s", "abcdef");
    assert(n == 6);
    assert(s[3] == '\0');
    puts("test_safe_sprintf: ok");
    return 0;
}
