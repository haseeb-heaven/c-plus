#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
int main(void) {
    char buf[16] = "abcdef";
    if (cplus_memmove_s(buf + 2, 14, buf, 3) != 0) return 1; /* overlap OK */
    if (memcmp(buf, "ababc", 5) != 0) return 1;
    puts("memmove_s: ok");
    return 0;
}
