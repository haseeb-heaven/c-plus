#include <stdio.h>
#include <string.h>
#include "safe_string.h"
int main(void) {
    char d[16] = "hi";
    if (cplus_strncat_s(d, sizeof(d), "world", 2) != 0) return 1;
    if (strcmp(d, "hiwo") != 0) return 1;
    puts("strncat_s: ok");
    return 0;
}
