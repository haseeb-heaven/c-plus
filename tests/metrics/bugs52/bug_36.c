#include <stdlib.h>
int main(void) { int* arr = (int*)malloc(10); arr[5] = 42; free(arr); return 0; }
