/* ADVERSARIAL mutation (THREAD-RACE): launder the shared write through a helper. */
#include <pthread.h>
static int shared_counter = 0;
static void bump(void) { shared_counter++; }
static void *worker(void *arg) { (void)arg; bump(); return 0; }
int main(void) { pthread_t a, b; pthread_create(&a, 0, worker, 0); pthread_create(&b, 0, worker, 0); pthread_join(a, 0); pthread_join(b, 0); return 0; }
