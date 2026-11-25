#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
int main(void){setenv("K","i",1);const char*p=getenv("K");for(int i=0;i<100;i++)setenv("F","x",1);printf("K=%s\n",p);return 0;}
