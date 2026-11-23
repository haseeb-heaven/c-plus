#include <stdlib.h>

int main(void){ int *p=malloc(16); if(!p)return 2; free(p); free(p); return 0; }
