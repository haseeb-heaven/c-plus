#include <stdlib.h>

int main(void)
{
    int *p = malloc(4 * sizeof(int));
    if (!p) return 2;
    p[9] = 42;
    free(p);
    return 0;
}
