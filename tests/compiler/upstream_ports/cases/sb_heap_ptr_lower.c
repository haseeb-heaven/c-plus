#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int *p = malloc(16);
    int *q;
    if (!p) return 2;
    q = p - argc;
    printf("%d\n", *q);
    free(p);
    return 0;
}
