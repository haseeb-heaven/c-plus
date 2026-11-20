/* Bug 11: realloc failure overwriting the original pointer. */
#include <stdlib.h>
int main(void) {
    int *arr = malloc(40);
    if (!arr) return 1;
    arr = realloc(arr, 1000);
    free(arr);
    return 0;
}
