// scenario: p0new_field_deep_uaf
// EXPECT: reject
// DIAG: released pointer
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct p0new_in { char *buf; };
struct p0new_mid { struct p0new_in in; };
struct p0new_out { struct p0new_mid m; };
void p0new_field_deep_uaf(void){
    struct p0new_out o;
    o.m.in.buf = malloc(16);
    if (!o.m.in.buf) return;
    free(o.m.in.buf);
    o.m.in.buf[0] = 'z';
}
