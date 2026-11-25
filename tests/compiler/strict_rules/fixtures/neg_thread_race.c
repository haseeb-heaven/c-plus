/* NEGATIVE threads: unsynchronized write to a shared global from a worker. */
#include <pthread.h>
static int shared_counter = 0;
static void *worker(void *arg) { (void)arg; for (int i = 0; i < 1000; i++) shared_counter++; return 0; }
int main(void) { pthread_t a, b; pthread_create(&a, 0, worker, 0); pthread_create(&b, 0, worker, 0); pthread_join(a, 0); pthread_join(b, 0); return 0; }
