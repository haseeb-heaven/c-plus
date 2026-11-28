#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define HEADER_SIZE 24
int main(void){uint32_t total=0; printf("len: "); if(scanf("%u",&total)!=1)return 1; char*raw=calloc(128,1); if(!raw)return 1; uint32_t plen=total-HEADER_SIZE; char*out=malloc(64); if(!out)return 1; memcpy(out,raw+HEADER_SIZE,plen); free(out); free(raw); return 0;}
