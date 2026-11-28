
#include <stdlib.h>
static void consume(int *p){ free(p); }
int main(void){ int *p = malloc(sizeof(int)); void(*f)(int*)=consume; f(p); free(p); return 0; }
