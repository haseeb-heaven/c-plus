/* Bug 396: busy-wait on a non-atomic, non-volatile flag written by threads. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>

static int done = 0;

static void *setter(void *arg) {
    (void)arg;
    done = 1;                          /* unsynchronised shared write */
    return NULL;
}

int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, setter, NULL);
    pthread_create(&b, NULL, setter, NULL);
    while (!done) { }                  /* load may be hoisted out of loop */
    printf("done=%d\n", done);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    return 0;
}
