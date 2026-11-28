/* Enumerated types: implicit, explicit and negative enumerators. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

enum Color { RED, GREEN = 5, BLUE, BLACK = -2 };

int main(void) {
    enum Color c = BLUE;
    printf("%d %d %d %d\n", RED, GREEN, BLUE, BLACK);
    printf("%d %d\n", (int)c, (int)(GREEN > RED));
    return 0;
}
