#include <stdlib.h>

int main(void) {
    int *p = malloc(64);
    if (!p) return 2;
    free(p + 2);
    return 0;
}
