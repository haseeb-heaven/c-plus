// scenario: p0new_out_uninit_ptr
// EXPECT: reject
// DIAG: uninitialized
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int p0new_acquire_fail(int **out){ (void)out; return -1; }
void p0new_out_uninit_ptr(void){
    int *slot;
    p0new_acquire_fail(&slot);
    (void)*slot;
}
