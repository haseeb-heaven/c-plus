// scenario: p0new_out_len_set_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void p0new_fill_len(int *dst, int *len){ *dst = 3; *len = 1; }
void p0new_out_len_set_ok(void){
    int buf[4] = {0};
    int n = 0;
    p0new_fill_len(buf, &n);
    buf[n] = 1;
}
