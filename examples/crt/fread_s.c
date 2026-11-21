#include <stdio.h>
#include <string.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = NULL;
    if (cplus_fopen_s(&f, "/tmp/cplus_fread_s.tmp", "wb+") != CPLUS_SAFE_OK) return 1;
    cplus_fwrite_s("abc", 3, 1, 3, f);
    rewind(f);
    char buf[8] = {0};
    size_t n = cplus_fread_s(buf, sizeof(buf), 1, 3, f);
    cplus_fclose_s(&f);
    remove("/tmp/cplus_fread_s.tmp");
    if (n != 3 || strcmp(buf, "abc") != 0) return 1;
    puts("fread_s: ok");
    return 0;
}
