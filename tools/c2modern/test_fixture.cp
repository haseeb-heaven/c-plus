/* c2modern: conservative additive conversion of test_fixture.c.
 * Original C source is preserved verbatim below this preamble.
 * Advisory call mapping: test_fixture.mapping (NO source call sites were rewritten).
 */
#include "stdlib/error.hp" /* c2modern: recoverable errors + checked allocator */
/* c2modern: stdlib/safe/safe_c.h is DOCUMENTED, NOT ENABLED, here.
 * Enabling it rewrites ordinary calls to checked _s entry points that live in
 * build/libc/libcplus_hardened.a and build/safe/libcplus_safecrt.a, and needs
 * -Ilibc/install-include -Istdlib/safe -Ilibc. The fixed verification command
 *   ./build/c+ build <out.cp> --strict --sanitize=none -I. -o <bin>
 * supplies none of those. This checkout also has a cplus_log macro/declaration
 * collision between stdlib/error.hp (cplus/cplus.h) and cplus_libc.h. See
 * test_fixture.mapping and README.md before enabling. */
#if 0
#include "stdlib/safe/safe_c.h"
#endif

/* Deterministic C input for tools/c2modern/convert.sh.
 *
 * Plain ISO C. Compiles with clang -std=c99 and with the C+ driver (C+ is a
 * superset of C). Contains a small, fixed set of ordinary libc calls so the
 * converter has predictable work to report.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_CAP 32

int main(int argc, char **argv)
{
    (void)argc; /* fixed argv is supplied by the verification step */
    (void)argv;

    char dst[BUF_CAP];
    const char src[] = "c2modern";

    memcpy(dst, src, sizeof(src)); /* ordinary -> checked + explicit bound */
    dst[BUF_CAP - 1] = '\0';

    strcpy(dst, src);  /* ordinary -> checked + destination capacity */
    strcat(dst, "-ok"); /* ordinary -> checked + destination capacity */

    void *heap = malloc(16); /* ordinary -> checked allocator */
    if (heap != NULL) {
        memset(heap, 0, 16); /* ordinary -> checked + capacity */
        free(heap);          /* ordinary -> checked deallocator */
    }

    printf("%s %d\n", dst, BUF_CAP); /* ordinary -> format-checked printf */
    printf("CHECKSUM=%d\n", (int)(sizeof(dst) + sizeof(src)));

    return 0;
}
