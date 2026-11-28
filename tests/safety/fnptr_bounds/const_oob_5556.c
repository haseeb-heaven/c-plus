#include <stdio.h>
typedef void (*handler_fn)(void);
static void op_nop(void){puts("NOP");}
static void op_read(void){puts("READ");}
static void op_exit(void){puts("EXIT");}
int main(void){handler_fn dispatch[]={op_nop,op_read,op_exit}; (void)dispatch[5556](); return 0;}
