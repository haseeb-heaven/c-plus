/* Harness validation: a non-zero exit code must be asserted exactly.
   (Additional case; not one of the C feature categories.) */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

int main(void) {
    printf("before exit\n");
    return 42;
}
