// scenario: p0new_ring_cursor_wrap_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_ring_cursor_wrap_ok(void){
    unsigned char head = 10;
    unsigned char tail = head + 3;
    char ring[16];
    ring[tail] = 0;
}
