/* Focused reproducer for the json_parse_file() budget bypass.
 *
 * Prints one line per fixture:
 *   <source> <depth> <file_valid> <buf_valid> <file_err_cat> <buf_err_cat>
 * A correct implementation must REFUSE every over-budget source (>16384
 * levels) on BOTH the file and the buffer path.  Before the fix the file path
 * accepted them (file_valid=1, file_err_cat=0).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

#define CFG_DEPTH ((size_t)16384u)
#define BCAP ((size_t)40000u + 64u)

static void make_depth(const char *path, size_t depth) {
    FILE *f = fopen(path, "wb");
    size_t i;
    if (f == (FILE *)0) return;
    for (i = 0; i < depth; i++) fputc('[', f);
    for (i = 0; i < depth; i++) fputc(']', f);
    fclose(f);
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : ".";
    static const size_t depths[] = {16000u, 16384u, 16385u, 20000u};
    static char buf[BCAP];
    size_t k;
    int rc = 0;
    for (k = 0; k < sizeof(depths) / sizeof(depths[0]); k++) {
        error ef = error();
        error eb = error();
        char path[256];
        size_t i, len = 0, d = depths[k];
        int fv, bv;
        snprintf(path, sizeof(path), "%s/repro_%zu.json", dir, d);
        make_depth(path, d);
        for (i = 0; i < d; i++) buf[len++] = '[';
        for (i = 0; i < d; i++) buf[len++] = ']';
        buf[len] = '\0';
        {
            json jf = json_parse_file(path, &ef);
            fv = jf.valid();
            if (fv) jf.drop();
        }
        {
            json jb = json_parse_budget(buf, len, CFG_DEPTH, &eb);
            bv = jb.valid();
            if (bv) jb.drop();
        }
        printf("json_parse  depth=%zu file_valid=%d buf_valid=%d file_errcat=%d buf_errcat=%d\n",
               d, fv, bv, ef.category(), eb.category());
        if (d > CFG_DEPTH && fv != 0) rc = 1; /* over-budget file must be refused */
    }
    return rc;
}
