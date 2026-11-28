/* Preprocessor macros: object/function-like, stringify, token paste. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

#define N 4
#define SQR(x) ((x) * (x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define STR(x) #x
#define CAT(a, b) a##b

int main(void) {
    int xy = 12;
    printf("%d %d %d\n", N, SQR(5), MIN(7, 3));
    printf("%s %s\n", STR(hello), STR(N));
    printf("%d\n", CAT(x, y));     /* expands to xy */
    return 0;
}
