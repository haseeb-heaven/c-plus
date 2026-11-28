#include <stdio.h>

struct record { int v[4]; };

int main(int argc, char **argv)
{
    struct record r = {{1, 2, 3, 4}};
    int i = argc + 8;
    (void)argv;
    printf("%d\n", r.v[i]);
    return 0;
}
