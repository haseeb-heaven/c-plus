#include <stdio.h>

int main(int argc, char **argv)
{
    int a[4] = {10, 20, 30, 40};
    int i = argc + 8;
    (void)argv;
    a[i] = 99;
    printf("%d\n", a[0]);
    return 0;
}
