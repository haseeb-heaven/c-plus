/*
 * lc28 - thread-local storage (C11 _Thread_local), basic object.
 *
 * Independent legacy-C ABI probe: file-scope _Thread_local scalar and array.
 * No threads are spawned (avoids a pthread link dependency); the probe verifies
 * that TLS objects have the same value semantics as ordinary static objects
 * and that their addresses are stable across calls.
 */
#include <stdio.h>

static _Thread_local int tls_counter = 0;
static _Thread_local unsigned char tls_bytes[4] = { 1, 2, 3, 4 };

static int bump(void)
{
    tls_counter += 3;
    return tls_counter;
}

int main(void)
{
    const unsigned char *p1 = tls_bytes;
    const unsigned char *p2 = tls_bytes;
    int a = bump();
    int b = bump();
    unsigned long sum = (unsigned long)tls_bytes[0] + tls_bytes[1] +
                        tls_bytes[2] + tls_bytes[3];

    if (a != 3 || b != 6 || tls_counter != 6 || sum != 10UL) {
        printf("lc28 result: FAIL a=%d b=%d counter=%d sum=%lu\n",
               a, b, tls_counter, sum);
        return 1;
    }
    if (p1 != p2) {
        printf("lc28 result: FAIL tls address unstable\n");
        return 1;
    }
    printf("lc28 result: OK a=%d b=%d counter=%d sum=%lu\n",
           a, b, tls_counter, sum);
    return 0;
}
