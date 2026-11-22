#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE*f(void){FILE*p=fopen("t","w");if(!p)return 0;return p;}
int main(void){FILE*p=f();(void)p;return 0;}
