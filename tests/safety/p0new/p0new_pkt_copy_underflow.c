// scenario: p0new_pkt_copy_underflow
// EXPECT: reject
// DIAG: copy of
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_pkt_copy_underflow(void){
    unsigned int wire = 3u;
    unsigned int body = wire - 10u;
    char src[8] = {0};
    char dst[8];
    memcpy(dst, src, body);
}
