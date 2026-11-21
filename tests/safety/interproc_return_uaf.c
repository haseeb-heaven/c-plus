#include <stdlib.h>
int *f(void){ int *p=malloc(sizeof(int)); free(p); return p; }  // NOSONAR intentional unsafe fixture for the C+ safe-mode checker
int main(void){ int *q=f(); *q=3; free(q); return 0; }
