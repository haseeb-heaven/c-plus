#include <stdlib.h>

int main(void)
{
    size_t n = (size_t)-1;
    int *p = malloc(n * 4);
    free(p);
    return 0;
}
