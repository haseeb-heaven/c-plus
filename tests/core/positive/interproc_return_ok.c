#include <stdlib.h>
int *f(void){ int *tmp=malloc(sizeof(int)); if(!tmp)return 0; free(tmp); int *p=malloc(sizeof(int)); return p; }
int main(void){ int *q=f(); if(!q)return 1; *q=3; free(q); return 0; }
