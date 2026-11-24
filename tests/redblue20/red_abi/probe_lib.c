#include <string.h>
#include <stdlib.h>
int red_probe_add(int a, int b) { return a + b; }
size_t red_probe_copy(char *d, const char *s) { strcpy(d, s); return strlen(d); }
void *red_probe_alloc(size_t n) { void *p = malloc(n); if (p) memset(p, 0, n); return p; }
