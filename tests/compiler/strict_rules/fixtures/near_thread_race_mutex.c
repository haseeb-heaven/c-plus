/* ADVERSARIAL near-miss (THREAD-RACE): the same shared write, mutex synchronized. */
#include <pthread.h>
static int shared_counter = 0;
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static void *worker(void *arg) { (void)arg; pthread_mutex_lock(&m); shared_counter++; pthread_mutex_unlock(&m); return 0; }
int main(void) { pthread_t a, b; pthread_create(&a, 0, worker, 0); pthread_create(&b, 0, worker, 0); pthread_join(a, 0); pthread_join(b, 0); return 0; }
