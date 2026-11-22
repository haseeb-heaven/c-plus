/* C consumer compatibility check for the nine ISO C headers in scope.
 * Uses only standard spellings; no C+ extension is required to compile. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%d/%d", INT_MAX, UINT8_MAX);
    assert(n > 0 && (size_t)n < sizeof buf);
    assert(strlen(buf) == (size_t)n);
    assert(isalpha('a') && !isalpha('0'));
    assert(toupper('z') == 'Z');
    errno = 0;
    (void)strtol("123", NULL, 10);
    assert(errno == 0);
    {
        void *p = malloc(16);
        assert(p != NULL);
        memset(p, 0x5a, 16);
        free(p);
    }
    puts("compat-c: ok");
    return 0;
}
