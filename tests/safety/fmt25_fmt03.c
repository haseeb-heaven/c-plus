#include <stdio.h>
int main(void){char in[64],out[128];if(fgets(in,sizeof(in),stdin))snprintf(out,sizeof(out),in);return 0;}
