#include <stdio.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = NULL;
    if (cplus_fopen_s(&f, "/tmp/cplus_fopen_s.tmp", "wb") != CPLUS_SAFE_OK) return 1;
    cplus_fclose_s(&f);
    remove("/tmp/cplus_fopen_s.tmp");
    puts("fopen_s: ok");
    return 0;
}
