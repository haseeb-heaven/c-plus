#include <stdlib.h>
static int g;
int main(void) { free(&g); return 0; }
