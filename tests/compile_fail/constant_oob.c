/* A statically known out-of-bounds subscript must be a compile error. */
#include <stdio.h>

int main(void) {
    int arr[] = {10, 30, 50};
    arr[10] = 20;
    printf("%d\n", arr[10]);
    return 0;
}
