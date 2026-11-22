// scenario: p0new_ring_cursor_wrap
// EXPECT: reject
// DIAG: constant array index
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_ring_cursor_wrap(void){
    unsigned char head = 250;
    unsigned char tail = head + 10;
    char ring[4];
    ring[tail] = 0;
}
