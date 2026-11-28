// scenario: p0new_field_bundle_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct p0new_node { int *value; };
void p0new_field_bundle_ok(void){
    struct p0new_node n[2];
    n[0].value = malloc(sizeof(int));
    n[1].value = malloc(sizeof(int));
    if (!n[0].value || !n[1].value) return;
    n[0].value[0] = 1;
    n[1].value[0] = 2;
    free(n[0].value);
    free(n[1].value);
}
