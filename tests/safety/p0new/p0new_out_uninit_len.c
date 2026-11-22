// scenario: p0new_out_uninit_len
// EXPECT: reject
// DIAG: uninitialized
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void p0new_fill_no_len(int *dst, int *len){ *dst = 3; }
void p0new_out_uninit_len(void){
    int buf[4];
    int n;
    p0new_fill_no_len(buf, &n);
    int taken = buf[n];
    (void)taken;
}
