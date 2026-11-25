/* Pointer conversions: object pointer <-> void*, char*, NULL. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

int main(void) {
    int x = 7;
    int *p = &x;
    void *v = p;                 /* implicit T* -> void* */
    int *q = (int *)v;           /* void* -> T* */
    char *c = (char *)p;         /* object pointer -> char* */
    void *n = 0;                 /* NULL */

    printf("%d %d\n", *q, (int)((void *)p == (void *)q));
    printf("%d\n", (int)(c == (char *)p));
    printf("%d\n", (int)(n == 0));
    return 0;
}
