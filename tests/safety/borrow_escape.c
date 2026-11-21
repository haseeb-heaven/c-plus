#include <stdio.h>
int *bad(void){int local=1;return &local;}
int main(void){(void)bad();return 0;}
