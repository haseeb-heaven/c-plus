#include <stdio.h>
int main(void){char b[32];fgets(b,sizeof(b),stdin);printf("Got: %s\n",b);return 0;}
