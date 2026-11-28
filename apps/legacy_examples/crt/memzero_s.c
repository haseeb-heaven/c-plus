#include <stdio.h>
#include "safe_memory.h"
int main(void) {
    char secret[8] = "secret";
    if (cplus_memzero_s(secret, sizeof(secret)) != 0) return 1;
    for (int i = 0; i < 8; ++i) if (secret[i] != 0) return 1;
    puts("memzero_s: ok");
    return 0;
}
