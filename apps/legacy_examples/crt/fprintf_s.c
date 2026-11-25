#include <stdio.h>
#include "safe_format.h"
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = cplus_fopen_s("/tmp/cplus_fprintf.tmp", "wb+");
    if (f == NULL) return 1;
    cplus_fprintf_s(f, "%s", "ok");
    cplus_fclose_s(f);
    remove("/tmp/cplus_fprintf.tmp");
    puts("fprintf_s: ok");
    return 0;
}
