#include <stdio.h>

static int g[4] = {5, 6, 7, 8};

int main(int argc, char **argv)
{
    int i = argc + 8;
    (void)argv;
    printf("%d\n", g[i]);
    return 0;
}
