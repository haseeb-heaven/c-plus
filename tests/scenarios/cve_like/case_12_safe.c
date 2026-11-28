/*
 * SAFE variant of case_12.c -- off-by-one write into a 24-byte heap buffer.
 *
 * Baseline (original case_12.c):
 *   `if (len <= 24) memcpy(bio, input, len + 1);` copies 25 bytes into the
 *   24-byte allocation when the stripped input is exactly 24 characters.
 *   24 'A' + '\n' input: ASan at -O1 reports nothing (exit 0, the optimizer
 *   lowered the copy); at -O0 it reports `heap-buffer-overflow`, WRITE of
 *   size 25, 0 bytes after the 24-byte region (case_12.c:8, exit 134).
 *
 * Mitigation:
 *   - Caller-side capacity check: `if (len >= 24) refuse;` so len + 1 <= 24
 *     and the terminating NUL always lands inside the destination.
 *   - The copy uses ordinary memcpy through the ordinary-C checked layer,
 *     which verifies destination capacity and refuses any write that would
 *     pass the end of the tracked 24-byte block.
 *
 * OUTCOME: a 24-character bio is refused; accepted bios are copied with the
 *          NUL strictly inside the 24-byte destination (no off-by-one, no UB).
 */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIO_CAP 24

int main(void)
{
    char *bio = (char *)malloc(BIO_CAP);
    char *next = (char *)malloc(BIO_CAP);
    char input[256];

    if (bio == NULL || next == NULL) {
        printf("OUTCOME: allocation failed\n");
        free(next);
        free(bio);
        return 1;
    }

    printf("bio: ");
    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("OUTCOME: no bio input\n");
    } else {
        size_t len = strlen(input);
        if (len && input[len - 1] == '\n') {
            input[--len] = 0;
        }
        if (len >= BIO_CAP) {
            printf("OUTCOME: bio refused (len=%zu >= 24)\n", len);
            free(next);
            free(bio);
            return 2;
        }
        /* len <= 23, so len + 1 <= 24 == BIO_CAP; checked by the layer. */
        memcpy(bio, input, len + 1);
        printf("OUTCOME: bounded bio copy completed (len=%zu)\n", len);
    }

    free(next);
    free(bio);
    return 0;
}
