#include <stdlib.h>
#include <stddef.h>
int main(void){ int*a=(int*)malloc(10*sizeof(int)); int*b=(int*)malloc(10*sizeof(int)); ptrdiff_t diff=b-a; (void)diff; free(a); free(b); return 0; }
