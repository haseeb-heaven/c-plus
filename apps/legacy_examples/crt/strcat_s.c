#include <stdio.h>
#include <string.h>
#include "safe_string.h"
int main(void) {
    char d[16] = "hi";
    if (cplus_strcat_s(d, sizeof(d), "!") != 0) return 1;
    if (strcmp(d, "hi!") != 0) return 1;
    if (cplus_strcat_s(d, 4, "world") == 0) return 1;
    puts("strcat_s: ok");
    return 0;
}
