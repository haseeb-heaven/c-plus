#include <stdio.h>
int main(void) {
    int x = 254;
    x += 558; x -= 91; x *= 2; x /= 2;
    int y = x++;
    int z = ++x;
    printf("x=%d y=%d z=%d\n", x, y, z);
    printf("mod=%d\n", x % 25);
    return 0;
}
