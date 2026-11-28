/* External SDK consumer: UNCHANGED ISO C spellings only.
 *
 * The only SDK-specific line is the include of the installed public header.
 * Every allocation/copy call below uses the ordinary C name (malloc, memset,
 * memcpy, strcpy, free). The header maps those names onto the plain hardened
 * libc layer; no `_s` spelling and no SafeCRT library is involved.
 */
#include <cplus_libc_safe.h>

#include <errno.h>
#include <stdio.h>

int main(void) {
    char *buf;
    char *small;

    setvbuf(stdout, NULL, _IONBF, 0);

    /* Ordinary malloc: a tracked 64-byte allocation. */
    buf = (char *)malloc(64);
    if (buf == NULL) {
        fputs("sdk consumer: malloc failed\n", stderr);
        return 1;
    }

    /* In-bounds ordinary writes still behave exactly as C. */
    memset(buf, 0x41, 64);
    if ((unsigned char)buf[0] != 0x41 || (unsigned char)buf[63] != 0x41) {
        fputs("sdk consumer: in-bounds memset failed\n", stderr);
        free(buf);
        return 1;
    }
    memcpy(buf, "sdk", 4);
    if (buf[0] != 's' || buf[1] != 'd' || buf[2] != 'k' || buf[3] != '\0') {
        fputs("sdk consumer: in-bounds memcpy failed\n", stderr);
        free(buf);
        return 1;
    }

    /* Over-capacity ordinary memset: refused, errno == ERANGE, no crash.
     * Only 16 bytes remain after buf + 48, so a 32-byte write must be
     * rejected before touching memory (buf[48] stays unchanged). */
    errno = 0;
    memset(buf + 48, 0x42, 32);
    if (errno != ERANGE || (unsigned char)buf[48] != 0x41) {
        fputs("sdk consumer: over-capacity memset was not refused\n", stderr);
        free(buf);
        return 1;
    }

    /* Over-capacity ordinary memcpy is refused the same way. */
    errno = 0;
    memcpy(buf + 60, "abcdefgh", 8); /* only 4 bytes remain */
    if (errno != ERANGE) {
        fputs("sdk consumer: over-capacity memcpy was not refused\n", stderr);
        free(buf);
        return 1;
    }

    /* Over-capacity ordinary strcpy is refused the same way. */
    small = (char *)malloc(4);
    if (small == NULL) {
        fputs("sdk consumer: second malloc failed\n", stderr);
        free(buf);
        return 1;
    }
    errno = 0;
    strcpy(small, "toolong"); /* needs 8 bytes, capacity is 4 */
    if (errno != ERANGE) {
        fputs("sdk consumer: over-capacity strcpy was not refused\n", stderr);
        free(small);
        free(buf);
        return 1;
    }

    free(small);
    free(buf);

    puts("sdk consumer: ok");
    return 0;
}
