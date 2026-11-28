/* Minimal genuine C11 feature probe.
 *
 * Exercises three features that do not exist in C99/C89:
 *   1. <stdatomic.h>  -> atomic_int / atomic_init / atomic_fetch_add / atomic_load
 *   2. _Static_assert -> compile-time assertion (C11 keyword)
 *   3. _Generic       -> compile-time type selection (C11 keyword)
 *
 * This is intentionally a real, runnable program: a correct C11 compiler
 * must compile it and it must print the single line below.
 */
#include <stdio.h>
#include <stdatomic.h>

_Static_assert(sizeof(int) >= 4, "int must be at least 32 bits");

static const char *describe(int x)
{
    return _Generic(x,
                    int: "int",
                    default: "other");
}

int main(void)
{
    atomic_int counter;
    atomic_init(&counter, 0);
    atomic_fetch_add(&counter, 7);
    int value = atomic_load(&counter);
    printf("%s counter=%d\n", describe(value), value);
    return 0;
}
