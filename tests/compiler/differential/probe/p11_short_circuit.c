#include <stdio.h>

int hit = 0;
int side(void) { hit++; return 1; }
int zero(void) { hit++; return 0; }

int main(void) {
    int r1 = zero() && side();
    printf("r1=%d hit=%d\n", r1, hit);

    hit = 0;
    int r2 = side() || side();
    printf("r2=%d hit=%d\n", r2, hit);

    hit = 0;
    int r3 = (0 && side()) + (1 || side());
    printf("r3=%d hit=%d\n", r3, hit);
    return 0;
}
