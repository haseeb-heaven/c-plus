#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "safe_string.h"
int main(void) {
    char d[8] = "a";
    assert(cplus_strcat_s(d, sizeof(d), "bc") == 0);
    assert(strcmp(d, "abc") == 0);
    assert(cplus_strcat_s(d, 4, "def") != 0);
    puts("unit strcat_s: ok");
    return 0;
}
