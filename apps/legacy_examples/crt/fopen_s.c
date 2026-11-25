#include <stdio.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = cplus_fopen_s("/tmp/cplus_fopen_s.tmp", "wb");
    if (f == NULL) return 1;
    cplus_fclose_s(f);
    remove("/tmp/cplus_fopen_s.tmp");
    puts("fopen_s: ok");
    return 0;
}
