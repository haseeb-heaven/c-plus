/* hexdump.cp -- deterministic bounded hex + ASCII dump, modern safe C+.
 *
 * Build (from the repository root):
 *   ./build/c+ build tools/dual/hexdump/hexdump.cp \
 *     --strict --sanitize=none -I. -o tools/dual/hexdump/out/hexdump_cp
 *
 * Usage:
 *   hexdump <path>     dump <path> (0 <= size <= HD_CAP bytes)
 *   hexdump -          refuse (no stdin fallback: an explicit path is required)
 *   hexdump            refuse (null/missing argument)
 *
 * Safety surface (runtime refusals, NOT a formal proof):
 *   - stdlib/safe/safe_c.h is included FIRST, so ordinary C spellings are
 *     routed through the checked SafeCRT/hardened-libc wrappers. A literal
 *     printf format carries a compile-time format attribute.
 *   - stdlib/file.hp owns the FILE* (RAII close); every byte enters through
 *     the checked file::read(char*, size, error*) API. A null handle, a null
 *     buffer or a stream error is reported through an error record instead of
 *     being dereferenced.
 *   - stdlib/error.hp carries a stable category + cplus_exit_code; refusals
 *     print that code so the evidence is observable.
 *   - hostile cases are refused with distinct nonzero exits:
 *       bad-args     exit 2, CPLUS_EXIT_UNSUPPORTED (108)
 *       cannot-open  exit 3, CPLUS_EXIT_IO          (109)
 *       oversized    exit 4, CPLUS_EXIT_BOUNDS      (101)
 *       io-error     exit 5, CPLUS_EXIT_IO          (109)
 *
 * Valid-input stdout is byte-identical to hexdump.c (every compiled C std)
 * and to the safe-Rust model. Matching output is NOT equivalent safety.
 */
#include "stdlib/safe/safe_c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"
#include "stdlib/string.hp"

#define HD_CAP 256
#define HD_LINE 16

static const char hd_hex[] = "0123456789abcdef";

/* Read argv[idx] without dereferencing an unverified pointer directly. */
static char *hd_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

/* Copy argv[1] into a caller-owned fixed buffer using bounded string ops. */
static int hd_copy_arg(char **argv, char *buf, size_t cap) {
    string text = string(hd_arg(argv, 1));
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
    char hex[HD_LINE * 3 + 1];
    char ascii[HD_LINE + 1];
    size_t n = 0;
    size_t idx;
    size_t h;
    size_t a;
    size_t k;
    unsigned long off;
    int rc;
    int oversized = 0;
    int io_error = 0;

    if (argc != 2) {
        error_fail(&err, CPLUS_EXIT_UNSUPPORTED, "bad-args");
        printf("HEXDUMP REFUSED bad-args code=%d\n", err.code());
        return 2;
    }
    if (!hd_copy_arg(argv, path, sizeof(path))) {
        error_fail(&err, CPLUS_EXIT_UNSUPPORTED, "bad-args");
        printf("HEXDUMP REFUSED bad-args code=%d\n", err.code());
        return 2;
    }
    if (path[0] == '-') {
        error_fail(&err, CPLUS_EXIT_UNSUPPORTED, "bad-args");
        printf("HEXDUMP REFUSED bad-args code=%d\n", err.code());
        return 2;
    }

    {
        file f = file(path, "rb");
        if (!f.is_open()) {
            error_fail(&err, CPLUS_EXIT_IO, "cannot-open");
            printf("HEXDUMP REFUSED cannot-open code=%d\n", err.code());
            return 3;
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
                /* The (HD_CAP+1)-th byte was consumed but never stored. */
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
        error_fail_cat(&err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                       "oversized");
        printf("HEXDUMP REFUSED oversized code=%d\n", err.code());
        return 4;
    }
    if (io_error) {
        error_fail(&err, CPLUS_EXIT_IO, "io-error");
        printf("HEXDUMP REFUSED io-error code=%d\n", err.code());
        return 5;
    }

    for (off = 0; off < (unsigned long)n; off += (unsigned long)HD_LINE) {
        h = 0;
        a = 0;
        for (k = 0; k < (size_t)HD_LINE; k++) {
            idx = (size_t)off + k;
            if (idx < n) {
                unsigned char b = (unsigned char)buf[idx];
                hex[h++] = hd_hex[b >> 4];
                hex[h++] = hd_hex[b & 15u];
                hex[h++] = ' ';
                ascii[a++] = (b >= 0x20u && b <= 0x7eu) ? (char)b : '.';
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
