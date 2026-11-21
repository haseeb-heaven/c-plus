#include <stdlib.h>
int main(void) { const char *s = "hello"; free((void *)s); return 0; }
