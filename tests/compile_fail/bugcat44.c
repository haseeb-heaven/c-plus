#define _POSIX_C_SOURCE 200809L
#include <stdio.h>

/* BUG 712: writing to a popen() pipe without checking fwrite()'s return
 * value.  A short write / SIGPIPE silently loses the data. */
void bug712_popen_unchecked_write(const char *data, size_t len) {
    FILE *f = popen("cat >/dev/null", "w");
    if (!f)
        return;
    fwrite(data, 1, len, f);
    pclose(f);
}

int main(void) { return 0; }
