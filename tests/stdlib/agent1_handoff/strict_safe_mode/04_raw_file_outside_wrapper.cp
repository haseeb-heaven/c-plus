// OP: raw FILE* operation outside the file wrapper.
// Uses stdio directly (fopen/fread/fclose) instead of the managed `file`
// wrapper. The handle may be NULL and the read length is caller-supplied, so
// neither liveness nor bounds are proven. A strict-safe mode would require the
// managed wrapper.
#include "cplus/cplus.h"
#include <stdio.h>

int main(int argc, char **argv) {
    FILE *f = fopen("/dev/null", "r");  /* raw FILE*, outside file.hp */
    char buf[16];
    size_t n = (size_t)argc;            /* caller-supplied length, unproven */
    (void)argv;
    if (f == NULL)
        return 1;
    (void)fread(buf, 1, n, f);          /* raw read into fixed buffer */
    fclose(f);
    return 0;
}
