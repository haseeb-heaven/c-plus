#include <stdio.h>

static int *getnull(void){ return 0; }
int main(void){ int *p=getnull(); if(p) printf("bad\n"); else printf("v=0\n"); return 0; }
