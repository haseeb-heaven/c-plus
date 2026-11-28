#include <stdio.h>
#include "safe_string.h"
int main(void) {
    int indicator = 99;
    if (cplus_strcmp_s("abc", 3, "abc", &indicator) != 0 || indicator != 0) return 1;
    if (cplus_strcmp_s("abc", 3, "abd", &indicator) != 0 || indicator >= 0) return 1;
    if (cplus_strcmp_s(NULL, 3, "abd", &indicator) == 0) return 1; /* constraint */
    puts("strcmp_s: ok");
    return 0;
}
