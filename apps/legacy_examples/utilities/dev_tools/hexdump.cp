/* hexdump.cp -- deterministic bounded hex + ASCII dump of a fixed input file.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/utilities/dev_tools/hexdump.cp \
 *     --strict --sanitize=none -I. -o apps/legacy_examples/utilities/dev_tools/hexdump
 *
 * Usage:
 *   hexdump <path>
 *
 * Safety evidence (runtime refusal, not a formal proof):
 *   - a missing/unopenable file is REFUSED ("cannot-open") instead of being
 *     read through a null handle;
 *   - a file larger than the fixed 256-byte buffer is REFUSED ("oversized")
 *     instead of being silently truncated.  A file that fits exactly is
 *     accepted (positive control).
 *
 * Every byte enters through the checked file::read API with an error* result;
 * no raw pointer arithmetic on argv is ever dereferenced (argv is copied into
 * a local buffer through string::substr first).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"

#define HD_CAP 256

static const char hd_hex[] = "0123456789abcdef";

static char *hd_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

/* Copy argv[idx] into a local fixed buffer; 0 on missing/oversized. */
static int hd_copy_arg(char **argv, size_t idx, char *buf, size_t cap) {
    string text = string(hd_arg(argv, idx));
    if (!text.valid()) { text.drop(); return 0; }
    if (text.size() + 1 > cap) { text.drop(); return 0; }
    if (text.substr(0, text.size(), buf, cap) != 0) { text.drop(); return 0; }
    text.drop();
    return 1;
}

int main(int argc, char **argv) {
    error err = error();
    char path[264];
    char buf[HD_CAP];
    char one[1];
    size_t n = 0;
    size_t off;
    int rc;
    int oversized = 0;
    int io_error = 0;

    if (argc != 2) {
        printf("HEXDUMP REFUSED bad-args\n");
        return 1;
    }
    if (!hd_copy_arg(argv, 1, path, sizeof(path))) {
        printf("HEXDUMP REFUSED bad-args\n");
        return 1;
    }

    {
        file f = file(path, "r");
        if (!f.is_open()) {
            printf("HEXDUMP REFUSED cannot-open\n");
            return 1;
        }
        for (;;) {
            rc = f.read(one, 1, &err);
            if (rc == STDLIB_EOF)
                break;
            if (rc != STDLIB_OK) {
                io_error = 1;
                break;
            }
            if (n >= HD_CAP) {
                /* One byte beyond capacity was consumed: refuse, never store. */
                oversized = 1;
                break;
            }
            __builtin_memcpy(buf + n, one, 1);
            n++;
        }
        if (!oversized && !io_error && f.close(&err) != STDLIB_OK)
            io_error = 1;
        f.drop();
    }

    if (oversized) {
        printf("HEXDUMP REFUSED oversized\n");
        return 1;
    }
    if (io_error) {
        printf("HEXDUMP REFUSED io-error\n");
        return 1;
    }

    printf("HEXDUMP %lu bytes\n", (unsigned long)n);
    for (off = 0; off < n; off += 16) {
        char hex[64];
        char ascii[20];
        size_t hi = 0;
        size_t ai = 0;
        size_t k;
        for (k = 0; k < 16; k++) {
            size_t idx = off + k;
            if (idx < n && idx < HD_CAP) {
                unsigned char b = (unsigned char)buf[idx];
                hex[hi++] = hd_hex[b >> 4];
                hex[hi++] = hd_hex[b & 15u];
                hex[hi++] = ' ';
                ascii[ai++] = (b >= 0x20u && b <= 0x7eu) ? (char)b : '.';
            } else {
                hex[hi++] = ' ';
                hex[hi++] = ' ';
                hex[hi++] = ' ';
            }
        }
        hex[hi] = '\0';
        ascii[ai] = '\0';
        printf("HEXDUMP %04lx  %s |%s|\n", (unsigned long)off, hex, ascii);
    }
    return 0;
}
