
#include <stdlib.h>
union U { void *p; int *i; };
int main(void){ union U u; u.p=malloc(sizeof(int)); free(u.p); return *u.i; }
