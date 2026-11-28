#include <stdio.h>

int main(int argc, char **argv)
{
    int s = argc + 40;
    int x = 1 << s;
    printf("%d\n", x);
    return 0;
}
