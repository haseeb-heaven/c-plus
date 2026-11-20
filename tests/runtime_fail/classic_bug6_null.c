/* Bug 6: null pointer dereference. */
#include <stdio.h>
int main(void) {
    int *ptr = NULL;
    *ptr = 10;
    printf("%d\n", *ptr);
    return 0;
}
