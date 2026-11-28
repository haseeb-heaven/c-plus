// scenario: p0new_s2u_guard_bypass
// EXPECT: reject
// DIAG: negative array index
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_s2u_guard_bypass(void){
    int delta = -1;
    char lane[8];
    if ((unsigned long)delta < sizeof lane) lane[delta] = 0;
}
