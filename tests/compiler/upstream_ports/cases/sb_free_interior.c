#include <stdlib.h>

int main(void)
{
    int *p = malloc(16);
    free(p + 1);
    return 0;
}
