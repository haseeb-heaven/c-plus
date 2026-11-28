/*
 * tests/abi/stdc_sdk/consumer.c
 *
 * A deliberately ORDINARY ISO C consumer. It uses only the four spellings
 * malloc / memcpy / strcpy / printf, and includes the SDK's installed header
 * ("cplus_libc.h"). The exact same translation unit is compiled twice:
 *
 *   1. plain  - installed headers + installed static (and shared) library
 *   2. checked - forced include of stdlib/safe/safe_c.h + the repo's checked
 *                archives (libcplus_safecrt.a + libcplus_hardened.a)
 *
 * For the default (valid) invocation both builds must print byte-identical
 * stdout. With argument "x" the hostile path performs a 40-byte memcpy into
 * an 8-byte stack object: the plain build has no check (undefined behaviour),
 * while the checked build must refuse the write (ESNOSPC 406, exit 70).
 *
 * No raw offsets, no machine paths, no cplus_* calls in the observable output.
 */
#include "cplus_libc.h" /* the installed SDK header (it also pulls in stdlib
                         * headers, so the program is plain ISO C) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int hostile = (argc > 1 && argv[1][0] == 'x');
    char *heap;
    char stack[16];

    /* ---- valid path: exercises malloc, memcpy, strcpy, printf ---------- */
    heap = (char *)malloc(32);
    if (heap == NULL) {
        fprintf(stderr, "consumer: malloc failed\n");
        return 2;
    }

    memcpy(heap, "libc-abi", 8); /* 8 bytes into a 32-byte heap block */
    heap[8] = '\0';
    strcpy(stack, "OK"); /* 3 bytes into a 16-byte stack object */

    /* Deterministic marker line used for byte-for-byte stdout comparison. */
    printf("consumer: heap=[%s] stack=[%s] n=%d\n", heap, stack, 42);

    /* ---- hostile path: only taken with an explicit argument ----------- */
    if (hostile) {
        char small[8];
        memset(small, 0, sizeof small);
        /* 40 bytes into an 8-byte object: plain C = silent stack corruption;
         * safe_c.h remaps memcpy to a capacity-checked wrapper that refuses. */
        memcpy(small, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 40);
        printf("consumer: hostile write completed (unchecked)\n");
    }

    free(heap);
    return 0;
}
