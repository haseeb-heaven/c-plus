// scenario: p0new_pkt_payload_underflow
// EXPECT: reject
// DIAG: negative array index
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_pkt_payload_underflow(void){
    uint16_t total = 4;
    uint16_t payload = total - 8;
    char frame[16];
    frame[payload] = 0;
}
