
#include <stdlib.h>
static void inner(int *p){ free(p); }
static void relay(int *p){ inner(p); }
int main(void){ int *p = malloc(sizeof(int)); relay(p); free(p); return 0; }
