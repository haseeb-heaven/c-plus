/* Bump allocations must be 16-byte aligned even for odd sizes and small
 * caller alignment requests; top must stay on the same grid. */
#include "cplus/cplus.h"
#include "stdlib/datastack.hp"

int main(void) {
    error e = error();
    stdlib_datastack ds;
    static const size_t sizes[5] = { 1u, 3u, 7u, 13u, 33u };
    static const size_t aligns[5] = { 1u, 2u, 4u, 8u, 16u };
    unsigned char *p[5];
    size_t i;
    int fails = 0;

    stdlib_set_error_callback((stdlib_error_callback)0);
    if (stdlib_datastack_init(&ds, 1024u, &e) != STDLIB_OK) return 2;
    for (i = 0; i < 5u; i++) {
        p[i] = (unsigned char *)stdlib_datastack_alloc(&ds, sizes[i], aligns[i], &e);
        if (p[i] == (unsigned char *)0) fails++;
        else {
            if (((unsigned long)p[i] & 15ul) != 0) fails++;
            p[i][0] = (unsigned char)(i + 1u);
        }
        if (((unsigned long)ds.top & 15ul) != 0) fails++;
    }
    for (i = 0; i < 5u; i++)
        if (p[i] != (unsigned char *)0 && p[i][0] != (unsigned char)(i + 1u)) fails++;
    stdlib_datastack_destroy(&ds, &e);
    print("ALIGN_TEST", fails);
    return fails ? 1 : 0;
}
