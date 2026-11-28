#include <stdlib.h>
static int *mk(void){ int *p=malloc(4*sizeof(int)); return p; }
int main(void){ int *q=mk(); if(!q)return 1; q[4]=1; return q[4]; }
