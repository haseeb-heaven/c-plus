// scenario: p0new_pkt_copy_underflow_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_pkt_copy_underflow_ok(void){
    unsigned int wire = 64u;
    unsigned int body = wire - 10u;
    char src[64] = {0};
    char dst[64];
    memcpy(dst, src, body);
}
