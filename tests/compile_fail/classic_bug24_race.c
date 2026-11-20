/* Bug 24: unsynchronised shared write in a thread. */
#include <pthread.h>
#include <stdio.h>
int shared_counter = 0;
void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++) shared_counter++;
    return NULL;
}
int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, worker, NULL);
    pthread_create(&b, NULL, worker, NULL);
    pthread_join(a, NULL); pthread_join(b, NULL);
    printf("%d\n", shared_counter);
    return 0;
}
