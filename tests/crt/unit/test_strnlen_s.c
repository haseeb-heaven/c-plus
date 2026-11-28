#include <assert.h>
#include <stdio.h>
#include "safe_string.h"
int main(void) {
    assert(cplus_strnlen_s("abc", 10) == 3);
    assert(cplus_strnlen_s("abc", 2) == 2);
    assert(cplus_strnlen_s(NULL, 10) == 0);
    puts("unit strnlen_s: ok");
    return 0;
}
