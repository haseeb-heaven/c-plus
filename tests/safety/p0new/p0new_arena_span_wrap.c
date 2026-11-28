// scenario: p0new_arena_span_wrap
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_arena_span_wrap(void){
    unsigned int stride = 0x40000000u;
    unsigned int span = stride * 4u;
    char pool[16];
    pool[span] = 1;
}
