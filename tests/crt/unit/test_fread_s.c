#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    const char *path = "/tmp/cplus_unit_fread.tmp";
    FILE *f = cplus_fopen_s(path, "wb+");
    assert(f != NULL);
    assert(fwrite("abc", 1, 3, f) == 3);
    rewind(f);
    char b[8] = {0};
    size_t n = cplus_fread_s(b, sizeof(b), 1, 3, f);
    assert(n == 3 && strcmp(b, "abc") == 0);
    cplus_fclose_s(f);
    unlink(path);
    puts("unit fread_s: ok");
    return 0;
}
