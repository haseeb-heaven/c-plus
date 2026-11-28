#include <stdio.h>
#include "safe_format.h"
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = cplus_fopen_s("/tmp/cplus_fscanf.tmp", "wb+");
    int v = 0;
    if (f == NULL) return 1;
    cplus_fprintf_s(f, "42");
    rewind(f);
    if (cplus_fscanf_s(f, "%d", &v) != 1 || v != 42) {
        cplus_fclose_s(f);
        remove("/tmp/cplus_fscanf.tmp");
        return 1;
    }
    cplus_fclose_s(f);
    remove("/tmp/cplus_fscanf.tmp");
    puts("fscanf_s: ok");
    return 0;
}
