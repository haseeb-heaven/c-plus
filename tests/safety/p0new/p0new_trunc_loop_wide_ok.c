// scenario: p0new_trunc_loop_wide_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_trunc_loop_wide_ok(void){
    unsigned int count = (unsigned int)0x100000005ULL;
    char slots[8];
    for (unsigned i = 0; i < count; ++i) slots[i] = 0;
}
