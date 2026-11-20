/* Bug 15: sizeof on a decayed array parameter. */
#include <stdio.h>
void clear_array(int arr[10]) {
    size_t count = sizeof(arr) / sizeof(arr[0]);
    printf("%zu\n", count);
}
int main(void) { int data[10] = {0}; clear_array(data); return 0; }
