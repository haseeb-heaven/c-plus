/* Bug 28: va_arg type mismatch after default promotions. */
#include <stdarg.h>
#include <stdio.h>
void f(int n, ...) { va_list ap; va_start(ap, n); float v = va_arg(ap, float); printf("%f\n", v); va_end(ap); }
int main(void) { f(1, 3.14f); return 0; }
