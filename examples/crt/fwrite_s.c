#include <stdio.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = NULL;
    if (cplus_fopen_s(&f, "/tmp/cplus_fwrite_s.tmp", "wb") != CPLUS_SAFE_OK) return 1;
    size_t n = cplus_fwrite_s("abc", 3, 1, 3, f);
    cplus_fclose_s(&f);
    remove("/tmp/cplus_fwrite_s.tmp");
    if (n != 3) return 1;
    puts("fwrite_s: ok");
    return 0;
}
