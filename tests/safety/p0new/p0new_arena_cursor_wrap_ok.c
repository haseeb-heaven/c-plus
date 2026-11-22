// scenario: p0new_arena_cursor_wrap_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_arena_cursor_wrap_ok(void){
    unsigned short off = 100;
    unsigned short end = off + 8;
    char arena[128];
    arena[end] = 0;
}
