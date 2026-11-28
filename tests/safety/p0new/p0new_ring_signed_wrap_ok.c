// scenario: p0new_ring_signed_wrap_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_ring_signed_wrap_ok(void){
    int cursor = 5;
    signed char idx = (signed char)cursor;
    char slots[8];
    slots[idx] = 0;
}
