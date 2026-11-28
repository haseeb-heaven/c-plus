#include <stdio.h>
#include "cplus/cplus.h"
resource struct F2 { FILE *h; };
void F2_drop(F2 *f) { if (f->h) fclose(f->h); }
FILE *bad_cast(void) {
    F2 f;
    f.h = fopen("/tmp/esc_cast.txt", "w");
    FILE *b = f.h;
    return (FILE *)b;   /* casted return of a borrowed pointer */
}
int main(void) { (void)bad_cast(); return 0; }
