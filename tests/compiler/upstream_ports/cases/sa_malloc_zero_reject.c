#include <stdlib.h>

int main(void)
{
    int *p = malloc(0);
    free(p);
    return 0;
}
