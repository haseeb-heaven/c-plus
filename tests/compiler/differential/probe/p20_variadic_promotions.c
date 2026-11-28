#include <stdio.h>
#include <stdarg.h>

void show_ints(int n, ...) {
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        int v = va_arg(ap, int);
        printf("%d ", v);
    }
    va_end(ap);
    printf("\n");
}

void show_doubles(int n, ...) {
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        double v = va_arg(ap, double);
        printf("%.1f ", v);
    }
    va_end(ap);
    printf("\n");
}

int main(void) {
    signed char a = -5;
    short b = 300;
    show_ints(2, a, b);

    float f = 1.5f;
    show_doubles(2, f, 2.25);
    return 0;
}
