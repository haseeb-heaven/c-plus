#include <stdio.h>
int main(void){char in[64];if(fgets(in,sizeof(in),stdin)){int(*fn)(const char*,...)=printf;fn(in);}return 0;}
