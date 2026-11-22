// scenario: p0new_out_checked_ptr_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int p0new_acquire_ok(int **out){
    *out = malloc(sizeof(int));
    return *out ? 0 : -1;
}
void p0new_out_checked_ptr_ok(void){
    int *p = 0;
    if (p0new_acquire_ok(&p) == 0 && p) *p = 1;
    free(p);
}
