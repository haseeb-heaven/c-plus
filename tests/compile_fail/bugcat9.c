/* Bug 355: relaxed atomic used to publish a non-atomic payload. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

static int g_payload = 0;
static atomic_int g_ready = 0;

static void *producer(void *arg) {
    (void)arg;
    g_payload++;                       /* unsynchronised shared write */
    atomic_store_explicit(&g_ready, 1, memory_order_relaxed);
    return NULL;
}

int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, producer, NULL);
    pthread_create(&b, NULL, producer, NULL);
    while (!atomic_load_explicit(&g_ready, memory_order_relaxed)) { }
    printf("payload=%d\n", g_payload); /* stale read: no acquire edge */
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    return 0;
}
