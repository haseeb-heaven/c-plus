
#include <stdlib.h>
int *id1(int *p){ return p; }
int *id2(int *p){ return id1(p); }
int main(void){ int *p=malloc(sizeof(int)); int *q=id2(p); free(p); return *q; }
