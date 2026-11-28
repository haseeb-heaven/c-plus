/* hexdump.c -- deterministic bounded hex + ASCII dump, strict ISO C89.
 *
 * Build (from the repository root):
 *   /usr/bin/clang -std=c89 -pedantic-errors -O2 \
 *       -o tools/dual/hexdump/out/hexdump_c_c89 tools/dual/hexdump/hexdump.c
 *
 * Usage:
 *   hexdump <path>     dump <path> (up to HD_CAP bytes)
 *   hexdump -          dump stdin
 *   hexdump            dump stdin (NO argument check -- see below)
 *
 * This is the deliberately "pure C" half of the dual demonstration.
 *
 *   - It uses no C99-only headers, types or comments: no // comments, no
 *     stdint.h, no declarations after statements, no for-loop declarations.
 *     It builds under -std=c89 -pedantic-errors (and c90/c99/c11/c17/c2x).
 *   - It has NO explicit refusal for the hostile cases. An unopenable path
 *     silently falls back to stdin; input beyond HD_CAP is silently ignored;
 *     a missing argument is treated as "read stdin". Those are exactly the
 *     conditions the C+ half refuses (see hexdump.cp / PROOF.md).
 *   - Its read loop is bounded here only by a hand-written `n < HD_CAP`
 *     condition; there is no checked-read API and no bounds metadata. This
 *     program is NOT memory-safe just because this one loop happens to stop.
 *
 * For all valid inputs (file openable, 0 <= size <= HD_CAP) stdout and exit
 * status are byte-identical to hexdump.cp and to the safe-Rust model.
 */
#include <stdio.h>
#include <stdlib.h>

#define HD_CAP 256
#define HD_LINE 16

static const char hd_hex[] = "0123456789abcdef";

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char buf[HD_CAP];
    char hex[HD_LINE * 3 + 1];
    char ascii[HD_LINE + 1];
    size_t n;
    size_t idx;
    size_t h;
    size_t a;
    size_t k;
    unsigned long off;
    int ch;

    /* No argument validation: argc==1 or argv[1]=="-" both mean stdin. */
    f = stdin;
    if (argc == 2 && argv[1] != NULL && argv[1][0] != '-') {
        f = fopen(argv[1], "rb");
    }
    /* No "cannot-open" refusal: a failed fopen silently becomes stdin.
     * With stdin redirected from /dev/null this yields an empty dump. */
    if (f == NULL) {
        f = stdin;
    }

    n = 0;
    while (n < HD_CAP) {
        ch = fgetc(f);
        if (ch == EOF) {
            break;
        }
        buf[n] = (unsigned char)ch;
        n++;
    }
    /* Any remaining input bytes are ignored; there is no oversized check. */

    for (off = 0; off < (unsigned long)n; off += (unsigned long)HD_LINE) {
        h = 0;
        a = 0;
        for (k = 0; k < (size_t)HD_LINE; k++) {
            idx = (size_t)off + k;
            if (idx < n) {
                unsigned char b = buf[idx];
                hex[h++] = hd_hex[b >> 4];
                hex[h++] = hd_hex[b & 0x0f];
                hex[h++] = ' ';
                if (b >= 0x20 && b <= 0x7e) {
                    ascii[a++] = (char)b;
                } else {
                    ascii[a++] = '.';
                }
            } else {
                hex[h++] = ' ';
                hex[h++] = ' ';
                hex[h++] = ' ';
            }
        }
        hex[h] = '\0';
        ascii[a] = '\0';
        printf("%08lx  %s  |%s|\n", off, hex, ascii);
    }
    return 0;
}
