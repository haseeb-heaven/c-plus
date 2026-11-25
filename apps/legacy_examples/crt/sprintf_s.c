#include <stdio.h>
#include <string.h>
#include "safe_format.h"
int main(void) {
    char b[16];
    int n = cplus_sprintf_s(b, sizeof(b), "%s-%d", "id", 7);
    if (n != 4 || strcmp(b, "id-7") != 0) return 1;
    char small[4];
    n = cplus_sprintf_s(small, sizeof(small), "%s", "toolong");
    if (n < 4) return 1; /* truncated; return value reports would-have-written length */
    if (small[3] != '\0') return 1;
    puts("sprintf_s: ok");
    return 0;
}
