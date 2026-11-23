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
