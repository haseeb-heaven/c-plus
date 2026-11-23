/* Variadic functions using <stdarg.h> (C89 va_list interface). */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>
#include <stdarg.h>

static int sum(int count, ...) {
    va_list ap;
    int i, total = 0;
    va_start(ap, count);
    for (i = 0; i < count; ++i)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

static void print_args(int count, ...) {
    va_list ap;
    int i;
    va_start(ap, count);
    for (i = 0; i < count; ++i)
        printf("%d%s", va_arg(ap, int), i + 1 < count ? "," : "");
    printf("\n");
    va_end(ap);
}

int main(void) {
    printf("%d %d\n", sum(3, 1, 2, 3), sum(0));
    print_args(4, 10, 20, 30, 40);
    printf("%d\n", printf("hi\n"));    /* printf returns 3 */
    return 0;
}
