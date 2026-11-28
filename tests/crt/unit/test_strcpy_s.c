#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "safe_string.h"
int main(void) {
    char d[8];
    assert(cplus_strcpy_s(d, sizeof(d), "hi") == 0);
    assert(strcmp(d, "hi") == 0);
    assert(cplus_strcpy_s(NULL, 8, "hi") != 0);
    assert(cplus_strcpy_s(d, 2, "hi") != 0);
    puts("unit strcpy_s: ok");
    return 0;
}
