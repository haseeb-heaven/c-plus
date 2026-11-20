#include <stdio.h>
#include "cplus/cplus.h"

resource struct File { FILE *h; };
void File_drop(File *f) { if (f->h) { fclose(f->h); f->h = NULL; } }

FILE *bad(void) {
    File f;
    f.h = fopen("/tmp/cplus_borrow_escape.txt", "w");
    FILE *borrow = f.h;
    return borrow;
}

int main(void) { (void)bad(); return 0; }
