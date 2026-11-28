#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_ERROR);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* 1. zeros full buffer */
    char a[8] = "secret!";
    assert(cplus_memzero_s(a, sizeof(a)) == 0);
    for (size_t i = 0; i < sizeof(a); i++) assert(a[i] == 0);

    /* 2. zeros partial prefix only */
    char b[6] = { 'A', 'B', 'C', 'D', 'E', 'F' };
    assert(cplus_memzero_s(b, 3) == 0);
    assert(b[0] == 0 && b[1] == 0 && b[2] == 0);
    assert(b[3] == 'D' && b[4] == 'E' && b[5] == 'F');

    /* 3. zero-length single-byte buffer */
    char c[1] = { 'Z' };
    assert(cplus_memzero_s(c, 1) == 0);
    assert(c[0] == 0);

    /* 4. zeros embedded nulls / binary data */
    unsigned char d[5] = { 0x01, 0x00, 0xFF, 0x7F, 0x80 };
    assert(cplus_memzero_s(d, sizeof(d)) == 0);
    for (size_t i = 0; i < sizeof(d); i++) assert(d[i] == 0);

    /* 5. NULL dest is rejected */
    assert(cplus_memzero_s(NULL, 8) != 0);

    /* 6. zero dmax is rejected */
    char e[4] = { 1, 2, 3, 4 };
    assert(cplus_memzero_s(e, 0) != 0);
    assert(e[0] == 1 && e[3] == 4);

    /* 7. RSIZE_MAX_MEM+1 is rejected */
    char f[4] = { 9, 9, 9, 9 };
    assert(cplus_memzero_s(f, RSIZE_MAX_MEM + 1) != 0);
    assert(f[0] == 9);

    /* 8. works on struct-like buffer */
    struct { int x; char y[3]; } s;
    s.x = 0x1234567;
    s.y[0] = 'a'; s.y[1] = 'b'; s.y[2] = 'c';
    assert(cplus_memzero_s(&s, sizeof(s)) == 0);
    unsigned char *sp = (unsigned char *)&s;
    for (size_t i = 0; i < sizeof(s); i++) assert(sp[i] == 0);

    /* 9. repeated zeroization stays OK */
    char g[4] = { 7, 8, 9, 10 };
    assert(cplus_memzero_s(g, sizeof(g)) == 0);
    assert(cplus_memzero_s(g, sizeof(g)) == 0);
    for (size_t i = 0; i < sizeof(g); i++) assert(g[i] == 0);

    /* 10. does not touch bytes beyond dmax */
    char h[5] = { 'a', 'b', 'c', 'd', 'e' };
    assert(cplus_memzero_s(h, 2) == 0);
    assert(h[0] == 0 && h[1] == 0);
    assert(h[2] == 'c' && h[3] == 'd' && h[4] == 'e');

    return 0;
}
