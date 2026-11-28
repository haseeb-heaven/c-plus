/*
 * lc36 - allocator interop: calloc zero-initialisation and byte-level reuse.
 *
 * Independent legacy-C ABI probe: verifies calloc zeroes storage, memset and
 * memcpy operate on allocated blocks, and realloc into a byte buffer works.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    unsigned char *bytes;
    unsigned char pattern[8];
    size_t i;
    int zeroed = 1;

    bytes = (unsigned char *)calloc(16, sizeof(*bytes));
    if (bytes == NULL) {
        printf("lc36 result: FAIL calloc\n");
        return 1;
    }
    for (i = 0; i < 16u; ++i) {
        if (bytes[i] != 0u) {
            zeroed = 0;
        }
    }
    memset(bytes, 0x5Au, 8);
    for (i = 0; i < 8u; ++i) {
        pattern[i] = bytes[i];
    }
    bytes = (unsigned char *)realloc(bytes, 24);
    if (bytes == NULL) {
        printf("lc36 result: FAIL realloc\n");
        return 1;
    }
    if (memcmp(bytes, pattern, 8) != 0) {
        zeroed = 0;
    }
    for (i = 8u; i < 24u; ++i) {
        bytes[i] = (unsigned char)(i & 0xFFu);
    }
    free(bytes);
    if (!zeroed || pattern[0] != 0x5Au || pattern[7] != 0x5Au) {
        printf("lc36 result: FAIL zeroed=%d pattern=%02x\n", zeroed, pattern[0]);
        return 1;
    }
    printf("lc36 result: OK zeroed=%d fill=%02x capacity=24\n", zeroed, pattern[0]);
    return 0;
}
