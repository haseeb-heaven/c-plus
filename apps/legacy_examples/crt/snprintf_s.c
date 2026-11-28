#include <stdio.h>
#include <string.h>
#include "safe_format.h"
int main(void) {
    char b[8];
    int n = cplus_snprintf_s(b, sizeof(b), "%s", "hi");
    if (n != 2 || strcmp(b, "hi") != 0) return 1;
    n = cplus_snprintf_s(b, 4, "%s", "toolong");
    if (n < 4 || strcmp(b, "too") != 0) return 1;
    puts("snprintf_s: ok");
    return 0;
}
