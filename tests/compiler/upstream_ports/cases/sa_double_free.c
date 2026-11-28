#include <stdlib.h>

int main(void)
{
    int *p = malloc(16);
    free(p);
    free(p);
    return 0;
}
