// scenario: p0new_arena_cursor_wrap
// EXPECT: reject
// DIAG: integer overflow in addition
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_arena_cursor_wrap(void){
    unsigned int off = 0xFFFFFFF0u;
    unsigned int end = off + 32u;
    char arena[8];
    arena[end] = 0;
}
