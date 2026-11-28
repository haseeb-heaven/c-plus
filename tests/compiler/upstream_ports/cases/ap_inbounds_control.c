#include <stdio.h>

int main(int argc, char **argv)
{
    int a[4] = {2, 4, 6, 8};
    int i = argc + 1;
    (void)argv;
    printf("%d\n", a[i]);
    return 0;
}
