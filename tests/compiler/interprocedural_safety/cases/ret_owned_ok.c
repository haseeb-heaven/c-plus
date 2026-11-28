#include <stdlib.h>
#include <stdio.h>

static int *make(void){ int *p=malloc(16); if(!p)return 0; p[0]=4; return p; }
int main(void){ int *q=make(); if(!q)return 2; printf("v=%d\n",q[0]); free(q); return 0; }
