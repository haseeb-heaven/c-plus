/* Deterministic preprocessor behavior; valid in every C standard. */
#include <stdio.h>

#define TWICE(x) ((x) + (x))
#define STR(x) #x
#define CAT(a, b) a##b
#define ENABLED 1

int main(void)
{
    int CAT(va, lue) = TWICE(21);

#if ENABLED
    printf("macro=%d str=%s\n", value, STR(TWICE(21)));
#else
    printf("disabled\n");
#endif
    return 0;
}
