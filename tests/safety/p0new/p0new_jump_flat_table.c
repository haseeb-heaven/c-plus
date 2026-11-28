// scenario: p0new_jump_flat_table
// EXPECT: reject
// DIAG: constant array index
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int (*p0new_jump_fn)(int);
static int step_up(int x){return x+1;}
static int step_down(int x){return x-1;}
int p0new_jump_flat_table(void){
    p0new_jump_fn table[2] = {step_up, step_down};
    return table[4](3);
}
