// scenario: p0new_field_bundle_uaf
// EXPECT: reject
// DIAG: released pointer
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct p0new_node { int *value; };
struct p0new_bundle { struct p0new_node slots[2]; };
void p0new_field_bundle_uaf(void){
    struct p0new_bundle b;
    b.slots[0].value = malloc(sizeof(int));
    if (!b.slots[0].value) return;
    free(b.slots[0].value);
    b.slots[0].value[0] = 9;
}
