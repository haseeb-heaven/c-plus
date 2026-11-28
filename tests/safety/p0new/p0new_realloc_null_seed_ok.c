// scenario: p0new_realloc_null_seed_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_realloc_null_seed_ok(void){
    int *p = realloc(0, sizeof(long) * 8);
    if (!p) return;
    p[0] = 1;
    free(p);
}
