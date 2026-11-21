#include <stdlib.h>
int main(void){ int *p=malloc(sizeof *p); if(!p) return 2; *p=9; int v=*p; free(p); return v==9?0:1; }
