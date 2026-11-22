// scenario: p0new_jump_flat_table_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int (*p0new_jump_fn)(int);
static int step_up(int x){return x+1;}
static int step_down(int x){return x-1;}
int p0new_jump_flat_table_ok(void){
    p0new_jump_fn table[2] = {step_up, step_down};
    return table[1](3);
}
