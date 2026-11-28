// scenario: p0new_ring_signed_wrap
// EXPECT: reject
// DIAG: constant array index
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_ring_signed_wrap(void){
    int cursor = 65535;
    signed char idx = (signed char)cursor;
    char slots[8];
    slots[idx] = 0;
}
