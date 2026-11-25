#include <stdlib.h>
int main(void){ int *p=malloc(sizeof(int)); if(!p)return 1; *p=42; int v=*p; free(p); return v==42?0:1; }
