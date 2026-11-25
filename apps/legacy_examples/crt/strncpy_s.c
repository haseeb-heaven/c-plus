#include <stdio.h>
#include <string.h>
#include "safe_string.h"
int main(void) {
    char d[8];
    if (cplus_strncpy_s(d, sizeof(d), "abcdef", 3) != 0) return 1;
    if (strcmp(d, "abc") != 0) return 1;
    puts("strncpy_s: ok");
    return 0;
}
