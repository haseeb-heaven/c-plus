/* Bug 26: pointer arithmetic beyond one-past-the-end. */
#include <stdio.h>
int main(void) { int arr[4] = {1,2,3,4}; int *p = arr + 5; printf("%p\n", (void *)p); return 0; }
