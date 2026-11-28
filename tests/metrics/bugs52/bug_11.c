#include <stdlib.h>
int main(void) { int* owner = (int*)malloc(sizeof(int)); int* observer = owner; free(owner); *observer = 50; return 0; }
