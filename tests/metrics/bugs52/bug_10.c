#include <stdlib.h>
int main(void) { int* p1 = (int*)malloc(2 * sizeof(int)); p1[0] = 10; int* p2 = (int*)realloc(p1, 10000 * sizeof(int)); if (p2) { p1[0] = 20; free(p2); } return 0; }
