// scenario: p0new_realloc_old_after_grow
// EXPECT: reject
// DIAG: realloc result and original pointer
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_realloc_old_after_grow(void){
    int *buf = malloc(16);
    if (!buf) return;
    int *grown = realloc(buf, 64);
    if (grown) buf[2] = 3;
    free(buf);
}
