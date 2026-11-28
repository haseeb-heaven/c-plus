#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    const char *path = "/tmp/cplus_safe_io_test.bin";
    FILE *f = cplus_fopen_s(path, "wb");
    assert(f != NULL);
    const char msg[] = "hello";
    assert(cplus_fwrite_s(msg, sizeof(msg), 1, sizeof(msg), f) == sizeof(msg));
    assert(cplus_fclose_s(f) == 0);
    assert(cplus_fclose_s(f) == EOF); /* double close refused */
    f = cplus_fopen_s(path, "rb");
    assert(f != NULL);
    char buf[16] = {0};
    assert(cplus_fread_s(buf, sizeof(buf), 1, sizeof(msg), f) == sizeof(msg));
    assert(strcmp(buf, "hello") == 0);
    assert(cplus_fclose_s(f) == 0);
    f = cplus_fopen_s("/no/such/cplus/path", "rb");
    assert(f == NULL);
    unlink(path);
    puts("test_safe_io: ok");
    return 0;
}
