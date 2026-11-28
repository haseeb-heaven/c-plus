#include <stdlib.h>
struct Holder { int* ptr; };
int main(void) { struct Holder* h = (struct Holder*)malloc(sizeof(struct Holder)); if (h->ptr != NULL) { *h->ptr = 10; } free(h); return 0; }
