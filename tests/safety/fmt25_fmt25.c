#include <stdio.h>
int main(void){char b[8];int n=snprintf(b,sizeof(b),"This string exceeds 8 bytes");b[n]='\0';return 0;}
