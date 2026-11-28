/* Conditional compilation: #if/#elif/#else/#ifdef/#ifndef and defined(). */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

#define FEATURE_A 1
#define LEVEL 2

int main(void) {
#if defined(FEATURE_A) && FEATURE_A
    printf("A");
#else
    printf("no-A");
#endif
#if LEVEL > 2
    printf(" high");
#elif LEVEL == 2
    printf(" mid");
#else
    printf(" low");
#endif
#ifdef NOT_DEFINED
    printf(" bad");
#endif
#ifndef NOT_DEFINED
    printf(" ok\n");
#endif
#ifdef __STDC__
    printf("stdc=%d\n", __STDC__);
#else
    printf("no-stdc\n");
#endif
    return 0;
}
