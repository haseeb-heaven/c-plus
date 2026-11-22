// scenario: p0new_s2u_capacity_trick
// EXPECT: reject
// DIAG: negative array index
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_s2u_capacity_trick(void){
    int back = -2;
    unsigned limit = 16u;
    char window[16];
    if ((unsigned)back < limit) window[back] = 7;
}
