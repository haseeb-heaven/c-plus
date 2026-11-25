#include <stdio.h>
#include "safe_memory.h"
int main(void) {
    int diff = 0;
    if (cplus_memcmp_s("abc", 3, "abc", 3, &diff) != 0 || diff != 0) return 1;
    if (cplus_memcmp_s("abc", 3, "abd", 3, &diff) != 0 || diff >= 0) return 1;
    puts("memcmp_s: ok");
    return 0;
}
