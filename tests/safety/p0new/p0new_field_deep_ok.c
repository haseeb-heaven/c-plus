// scenario: p0new_field_deep_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct p0new_in { int *v; };
struct p0new_holder { struct p0new_in i; };
void p0new_field_deep_ok(void){
    struct p0new_holder h;
    h.i.v = malloc(sizeof(int));
    if (!h.i.v) return;
    h.i.v[0] = 1;
    free(h.i.v);
}
