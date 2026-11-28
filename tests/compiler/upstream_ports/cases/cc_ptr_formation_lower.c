#include <stdio.h>

int main(int argc, char **argv)
{
    int a[4] = {1, 2, 3, 4};
    int *p = a - argc;
    (void)argv;
    printf("%d\n", *p);
    return 0;
}
