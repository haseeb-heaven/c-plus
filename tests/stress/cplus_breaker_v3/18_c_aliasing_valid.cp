#include <stdio.h>
#include <string.h>
struct Packet { unsigned char bytes[8]; };
int main(void){ struct Packet p={.bytes={1,2,3,4,5,6,7,8}}; unsigned char out[8]={0}; memcpy(out,p.bytes,sizeof out); unsigned sum=0; for(int i=0;i<8;i++)sum+=out[i]; printf("ALIAS %u\n",sum);return sum==36?0:1; }
