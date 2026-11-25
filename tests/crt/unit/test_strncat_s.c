#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "safe_string.h"
int main(void) {
    char d[8] = "a";
    assert(cplus_strncat_s(d, sizeof(d), "bcdef", 2) == 0);
    assert(strcmp(d, "abc") == 0);
    assert(cplus_strncat_s(NULL, 8, "b", 1) != 0);
    puts("unit strncat_s: ok");
    return 0;
}
