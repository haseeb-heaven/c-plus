/* Byte-level reproducers for the two read_line findings (evidence only). */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"
#include "stdlib/file.hp"

static void dump(const char *tag, const char *p, size_t n) {
    size_t i;
    printf("%s:", tag);
    for (i = 0; i < n; i++) printf(" %02x", (unsigned char)p[i]);
    printf("\n");
}
static void run(const char *name, const char *path, const char *in, size_t inlen,
                size_t size) {
    FILE *w = fopen(path, "wb");
    size_t wrote;
    file f;
    error e = error();
    char buf[8];
    int rc, c;
    char rem[16];
    size_t rn = 0;
    if (w == (FILE *)0) return;
    wrote = fwrite(in, 1, inlen, w);
    (void)wrote;
    (void)fclose(w);
    memset(buf, 0xA5, sizeof(buf));
    f = file(path, "rb");
    error_reset(&e);
    rc = f.read_line(buf, size, &e);
    while ((c = fgetc(f.native())) != EOF && rn < sizeof(rem)) rem[rn++] = (char)c;
    printf("%s size=%zu rc=%d failed=%d\n", name, size, rc, e.failed_now());
    dump("  in ", in, inlen);
    dump("  buf", buf, 8);
    dump("  rem", rem, rn);
    f.drop();
}
int main(void) {
    run("F1_CR_EXACT", "tests/redblue20/red_io/out/f1.bin", "ab\rcd\n", 6u, 4u);
    run("F2_EMBEDDED_NUL", "tests/redblue20/red_io/out/f2.bin", "aa\0bbb\n", 7u, 4u);
    return 0;
}
