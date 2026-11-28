#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "safe_string.h"
int main(void) {
    char d[8];
    assert(cplus_strncpy_s(d, sizeof(d), "abcdef", 3) == 0);
    assert(strcmp(d, "abc") == 0);
    assert(cplus_strncpy_s(NULL, 8, "a", 1) != 0);
    puts("unit strncpy_s: ok");
    return 0;
}
