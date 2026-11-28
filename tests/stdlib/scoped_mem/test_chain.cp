/* Chained data-stack regression: a bump frame that overflows its first chunk
 * spills into chained arena blocks instead of failing or corrupting memory.
 * Prints CHAIN_TEST <fails>. */
#include "cplus/cplus.h"
#include "stdlib/scoped_mem.hp"

int main(void) {
    error e = error();
    stdlib_datastack_chained c;
    unsigned char *a = (unsigned char *)0;
    unsigned char *b = (unsigned char *)0;
    unsigned char *d = (unsigned char *)0;
    unsigned char *saved = (unsigned char *)0;
    size_t i;
    size_t blocks;
    int fails = 0;

    stdlib_set_error_callback((stdlib_error_callback)0);
    if (stdlib_datastack_chained_init(&c, 256u, &e) != 0) return 2;

    {
        stdlib_datastack_chained_scope frame = stdlib_datastack_chained_scope(&c);
        saved = c.stack.top;
        a = (unsigned char *)stdlib_datastack_chained_alloc(&c, 128u, 16u, &e);
        b = (unsigned char *)stdlib_datastack_chained_alloc(&c, 512u, 16u, &e);
        d = (unsigned char *)stdlib_datastack_chained_alloc(&c, 512u, 16u, &e);
        if (a == (unsigned char *)0 || b == (unsigned char *)0 || d == (unsigned char *)0) fails++;
        if (a != (unsigned char *)0 && (((unsigned long)a) & 15ul) != 0) fails++;
        if (b != (unsigned char *)0 && (((unsigned long)b) & 15ul) != 0) fails++;
        if (d != (unsigned char *)0 && (((unsigned long)d) & 15ul) != 0) fails++;
        blocks = stdlib_datastack_chained_blocks(&c);
        if (blocks < 3u) fails++; /* first chunk + at least two spill blocks */
        if (a != (unsigned char *)0) for (i = 0; i < 128u; i++) a[i] = 0x5Au;
        if (b != (unsigned char *)0) for (i = 0; i < 512u; i++) b[i] = 0x6Bu;
        if (d != (unsigned char *)0) for (i = 0; i < 512u; i++) d[i] = 0x7Cu;
        if (!stdlib_datastack_chained_is_live(&c, a)) fails++;
        if (!stdlib_datastack_chained_is_live(&c, b)) fails++;
        if (!stdlib_datastack_chained_is_live(&c, d)) fails++;
        frame.drop();
        if (c.stack.top != saved) fails++;
        if (stdlib_datastack_chained_is_live(&c, a)) fails++; /* stale refused */
        if (stdlib_datastack_chained_is_live(&c, b)) fails++;
        if (stdlib_datastack_chained_is_live(&c, d)) fails++;
        if (stdlib_datastack_poison_enabled() && a != (unsigned char *)0)
            for (i = 0; i < 128u; i++) if (a[i] != 0xDDu) fails++;
    }

    /* The rewound bump chunk is reusable after the frame drops. */
    {
        stdlib_datastack_chained_scope frame = stdlib_datastack_chained_scope(&c);
        unsigned char *reused = (unsigned char *)stdlib_datastack_chained_alloc(&c, 128u, 16u, &e);
        if (reused != a) fails++;
        frame.drop();
    }

    stdlib_datastack_chained_destroy(&c, &e);
    print("CHAIN_TEST", fails);
    return fails ? 1 : 0;
}
