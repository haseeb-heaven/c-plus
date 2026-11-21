#include <stdlib.h>
int main(void) { char* raw = (char*)malloc(16); int* unaligned = (int*)(raw + 1); *unaligned = 42; free(raw); return 0; }
