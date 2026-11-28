// POSITIVE CONTROL (not one of the six matrix operations).
// This is a documented unverified operation: a non-literal printf format.
// The compiler's AstSafetyCheck warns in DEFAULT and fails closed in STRICT.
// Its presence proves the --strict flag is wired up, so an ACCEPT in both
// modes for cases 01-06 is a genuine capability gap, not a dead flag.
#include "cplus/cplus.h"
#include <stdio.h>

int main(int argc, char **argv) {
    const char *fmt = argc > 1 ? argv[1] : "%d";
    printf(fmt, 42);   /* non-literal format: warn (default) / error (strict) */
    return 0;
}
