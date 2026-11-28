#include <stdlib.h>

int main(void)
{
    int *p = malloc(16);
    if (!p) return 2;
    free(p);
    *p = 7;
    return 0;
}
