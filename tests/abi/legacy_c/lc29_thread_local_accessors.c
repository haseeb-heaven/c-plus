/*
 * lc29 - thread-local storage: accessor functions and global/TLS separation.
 *
 * Independent legacy-C ABI probe: _Thread_local state manipulated only through
 * exported-style helpers, contrasted with an ordinary global counter.
 */
#include <stdio.h>

static _Thread_local int tls_slot;
static int g_shared_slot;

int tls_store(int value)
{
    int previous = tls_slot;
    tls_slot = value;
    g_shared_slot += 1;
    return previous;
}

int tls_load(void)
{
    return tls_slot;
}

int tls_exchange_global(void)
{
    int old = tls_slot;
    tls_slot = g_shared_slot * 10;
    return old;
}

int main(void)
{
    int p1, p2, old;

    p1 = tls_store(11);
    p2 = tls_store(22);
    old = tls_exchange_global();
    if (p1 != 0 || p2 != 11 || tls_load() != 20 || old != 22 ||
        g_shared_slot != 2) {
        printf("lc29 result: FAIL p1=%d p2=%d tls=%d old=%d global=%d\n",
               p1, p2, tls_load(), old, g_shared_slot);
        return 1;
    }
    printf("lc29 result: OK p1=%d p2=%d tls=%d old=%d global=%d\n",
           p1, p2, tls_load(), old, g_shared_slot);
    return 0;
}
