#include <stdlib.h>
int main(void) { int* p = (int*)malloc(0); if (p != NULL) { *p = 10; free(p); } return 0; }
