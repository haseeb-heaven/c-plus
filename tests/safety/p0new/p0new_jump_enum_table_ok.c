// scenario: p0new_jump_enum_table_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void (*p0new_cb)(void);
static void action_a(void){}
static void action_b(void){}
enum { P0NEW_A, P0NEW_B, P0NEW_N };
void p0new_jump_enum_table_ok(void){
    p0new_cb handlers[P0NEW_N] = {action_a, action_b};
    for (int i = 0; i < P0NEW_N; ++i) handlers[i]();
}
