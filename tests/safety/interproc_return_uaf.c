#include <stdlib.h>
int *f(void){ int *p=malloc(sizeof(int)); free(p); return p; }
int main(void){ int *q=f(); *q=3; free(q); return 0; }
