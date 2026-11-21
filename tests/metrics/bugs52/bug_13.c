#include <stdlib.h>
int work(int error_flag) { int* buf = (int*)malloc(100 * sizeof(int)); if (!buf) return -1; if (error_flag) { return -1; } free(buf); return 0; }
int main(void) { return work(1); }
