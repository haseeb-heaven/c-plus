#include <stdio.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = NULL;
    if (cplus_fopen_s(&f, "/tmp/cplus_fclose_s.tmp", "wb") != CPLUS_SAFE_OK) return 1;
    if (cplus_fclose_s(&f) != CPLUS_SAFE_OK || f != NULL) return 1;
    remove("/tmp/cplus_fclose_s.tmp");
    puts("fclose_s: ok");
    return 0;
}
