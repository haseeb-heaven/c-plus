// scenario: p0new_arena_span_wrap_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_arena_span_wrap_ok(void){
    unsigned int stride = 1024u;
    unsigned int span = stride * 4u;
    char pool[8192];
    pool[span] = 1;
}
