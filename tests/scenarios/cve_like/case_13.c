/* Original user-supplied crash scenario 13 (minimally adjusted to compile). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
typedef void (*fn)(void);
static void op_nop(void){puts("NOP");} static void op_read(void){puts("READ");} static void op_exit(void){puts("EXIT");}
int main(void){ fn d[]={op_nop,op_read,op_exit}; size_t cmd=0; printf("cmd: "); if(scanf("%zu",&cmd)!=1) return 1; d[cmd](); return 0; }
