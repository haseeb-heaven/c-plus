#include <stdint.h>
#include <stdlib.h>
struct Arena { char*base; size_t offset; };
void*arena_alloc(struct Arena*a,size_t size){ void*ptr=a->base+a->offset; a->offset+=size; return ptr; }
int main(void){ struct Arena a={(char*)malloc(1024),0}; char*c=(char*)arena_alloc(&a,1); *c='A'; double*d=(double*)arena_alloc(&a,sizeof(double)); *d=3.14; free(a.base); return 0; }
