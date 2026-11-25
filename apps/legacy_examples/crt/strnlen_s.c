#include <stdio.h>
#include "safe_string.h"
int main(void) {
    if (cplus_strnlen_s("abcd", 10) != 4) return 1;
    if (cplus_strnlen_s(NULL, 10) != 0) return 1;
    puts("strnlen_s: ok");
    return 0;
}
