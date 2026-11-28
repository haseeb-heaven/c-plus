// scenario: p0new_realloc_lost_result
// EXPECT: reject
// DIAG: raw realloc return value is ignored
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_realloc_lost_result(void){
    int *block = malloc(32);
    if (!block) return;
    realloc(block, 256);
    block[0] = 7;
    free(block);
}
