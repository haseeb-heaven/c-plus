/* Gate 08: data race (two threads mutating shared state without synchronisation).
 * Expected outcome class: not_proven (runner sees exit 0 with a wrong final count).
 * C+ note: the hardened libc synchronises its own metadata only, not caller
 * shared variables. ThreadSanitizer (unavailable here), not C+ or UBSan, is the
 * tool that detects this class. No diagnostic directives are used.
 */
#include <stdio.h>
#include <pthread.h>

static int shared = 0;

static void *worker(void *arg) {
    (void)arg;
    int i;
    for (i = 0; i < 100000; ++i) shared++;
    return NULL;
}

int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, worker, NULL);
    pthread_create(&b, NULL, worker, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    printf("gate08: shared=%d (expected 200000)\n", shared);
    return 0;
}
