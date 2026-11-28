/* Bug 625 (widening index into a small array): a 64-bit long index is
   used unchecked to subscript a four-element array. */
#include <stdio.h>

int main(void) {
    int a[4] = {1, 2, 3, 4};
    long i = 9;
    printf("%d\n", a[i]);
    return 0;
}
