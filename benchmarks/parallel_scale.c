/* Benchmark: pthread mutex contention and scaling. */
#include <pthread.h>
#include <stdio.h>

#define THREADS 4
#define PER_THREAD 100000

typedef struct shared {
    pthread_mutex_t mutex;
    long counter;
} shared;

static shared state;

static void *worker(void *arg) {
    (void)arg;
    int i;
    for (i = 0; i < PER_THREAD; i++) {
        pthread_mutex_lock(&state.mutex);
        state.counter++;
        pthread_mutex_unlock(&state.mutex);
    }
    return NULL;
}

int main(void) {
    pthread_t threads[THREADS];
    int i;
    pthread_mutex_init(&state.mutex, NULL);
    for (i = 0; i < THREADS; i++)
        pthread_create(&threads[i], NULL, worker, NULL);
    for (i = 0; i < THREADS; i++)
        pthread_join(threads[i], NULL);
    printf("counter=%ld\n", state.counter);
    pthread_mutex_destroy(&state.mutex);
    return 0;
}
