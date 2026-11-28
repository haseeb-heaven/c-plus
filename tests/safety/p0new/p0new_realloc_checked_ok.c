// scenario: p0new_realloc_checked_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_realloc_checked_ok(void){
    int *p = malloc(16);
    if (!p) return;
    int *r = realloc(p, 64);
    if (!r) { free(p); return; }
    p = r;
    p[2] = 3;
    free(p);
}
