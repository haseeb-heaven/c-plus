#include <stdio.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = cplus_fopen_s("/tmp/cplus_fclose_s.tmp", "wb");
    if (f == NULL) return 1;
    if (cplus_fclose_s(f) != 0) return 1;
    remove("/tmp/cplus_fclose_s.tmp");
    puts("fclose_s: ok");
    return 0;
}
