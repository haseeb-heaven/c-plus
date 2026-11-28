/* Compile-only source for the instrumented-runtime linker-flag regression.
 *
 * The driver recompiles the C+ runtime with -c for a sanitized build.  The
 * regression passes -ldefinitely_nonexistent (a valid application build flag,
 * invalid for a compile-only invocation) alongside this source.  The -D guard
 * below also proves that compile-affecting pass-through flags survive the
 * linker-flag filter.  The program is deliberately never linked or run. */
#ifndef CPLUS_IRLF_DEFINE_SEEN
#error "pass-through -D flag was dropped from the compile"
#endif

#include <stdio.h>

int main(void) {
    printf("instrumented runtime linker flag regression\n");
    return 0;
}
