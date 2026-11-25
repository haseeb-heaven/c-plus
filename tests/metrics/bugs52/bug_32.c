#include <stdlib.h>
int main(void) { void* p = malloc(128); p = realloc(p, (size_t)-1); free(p); return 0; }
