#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static void *worker(void *arg) {
    int *counter = (int *)arg;
    for (int i = 0; i < 200000; i++) {
        (*counter)++;
    }
    return NULL;
}

int main(void) {
    int *counter = (int *)calloc(1, sizeof(int));
    if (!counter) return 1;
    pthread_t t1, t2;
    pthread_create(&t1, NULL, worker, counter);
    pthread_create(&t2, NULL, worker, counter);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    if (*counter != 400000) {
        printf("BREACH: data race lost updates (got %d / expected 400000)\n", *counter);
    } else {
        printf("RACE_COMPILED: shared mutable heap state ran without sync (%d)\n", *counter);
    }
    free(counter);
    return 0;
}
