#include <stdlib.h>
int main(void) { int* arr = (int*)malloc(5 * sizeof(int)); arr[5] = 99; free(arr); return 0; }
