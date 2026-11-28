#include <stdlib.h>
int main(void){ int *p = malloc(1); p[9] = 42; return p[9]; }
