#include <stdio.h>
typedef void (*handler_fn)(void);
static void op_nop(void){puts("NOP");}
static void op_read(void){puts("READ");}
static void op_exit(void){puts("EXIT");}
int main(void){handler_fn dispatch[]={op_nop,op_read,op_exit}; size_t cmd=0; if(scanf("%zu",&cmd)!=1)return 1; dispatch[cmd](); return 0;}
