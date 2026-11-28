#include <stdio.h>
#include <stdlib.h>

int main(void){ int *p=malloc(16); if(!p)return 2; free(p); printf("v=%d\n", p[0]); return 0; }
