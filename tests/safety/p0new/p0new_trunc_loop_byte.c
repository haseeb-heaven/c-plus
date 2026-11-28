// scenario: p0new_trunc_loop_byte
// EXPECT: reject
// DIAG: out-of-bounds access: index
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_trunc_loop_byte(void){
    uint8_t cap = (uint8_t)300u;
    char table[4];
    for (int k = 0; k < cap; ++k) table[k] = 0;
}
