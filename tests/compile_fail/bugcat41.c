/* Bug 676: unsynchronized write to a shared global from thread functions. */
#include <pthread.h>
#include <stdio.h>

int hits = 0;

void *bump(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++)
        hits++;
    return NULL;
}

int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, bump, NULL);
    pthread_create(&b, NULL, bump, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    printf("%d\n", hits);
    return 0;
}
