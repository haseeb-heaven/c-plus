// scenario: p0new_pkt_payload_underflow_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_pkt_payload_underflow_ok(void){
    uint16_t total = 32;
    uint16_t payload = total - 8;
    char frame[32];
    frame[payload] = 0;
}
