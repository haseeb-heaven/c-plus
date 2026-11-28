#include <stdlib.h>

int main(void)
{
    int *p = malloc(16);
    int *q = realloc(p, 32);
    p[0] = 1;
    free(q);
    return 0;
}
