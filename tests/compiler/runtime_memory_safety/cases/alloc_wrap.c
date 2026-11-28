#include <stdlib.h>
#include <stdint.h>

int main(void){ size_t n=SIZE_MAX/2; int *p=malloc(n*sizeof(int)); if(!p)return 2; free(p); return 0; }
