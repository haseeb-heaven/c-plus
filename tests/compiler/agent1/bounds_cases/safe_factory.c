#include <stdlib.h>
#include <stdio.h>
static int *mk(void){ int *p=malloc(4*sizeof(int)); return p; }
int main(void){ int *q=mk(); if(!q)return 1; q[3]=5; printf("%d\n",q[3]); free(q); return 0; }
