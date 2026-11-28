// scenario: p0new_trunc_loop_wide
// EXPECT: reject
// DIAG: out-of-bounds access: index
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_trunc_loop_wide(void){
    unsigned int count = (unsigned int)0x100000005ULL;
    char slots[4];
    for (unsigned i = 0; i < count; ++i) slots[i] = 0;
}
