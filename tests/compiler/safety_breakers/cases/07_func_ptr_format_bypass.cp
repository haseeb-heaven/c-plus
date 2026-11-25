#include <stdio.h>

int main(void) {
    int (*fn)(const char *, ...) = printf;
    char input[32] = "%x %x %x %x\n";
    fn(input);
    printf("BREACH: func ptr bypassed format string check\n");
    return 0;
}
