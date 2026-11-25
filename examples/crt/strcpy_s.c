#include <stdio.h>
#include <string.h>
#include "safe_string.h"
int main(void) {
    char name[32];
    if (cplus_strcpy_s(name, sizeof(name), "C+") != 0) return 1;
    if (strcmp(name, "C+") != 0) return 1;
    char small[4];
    if (cplus_strcpy_s(small, sizeof(small), "Too long") == 0) return 1;
    puts("strcpy_s: ok");
    return 0;
}
