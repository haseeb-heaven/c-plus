/* POSITIVE threads: worker touches only its own stack, no shared global. */
#include <pthread.h>
static void *worker(void *arg) { (void)arg; int local = 0; local++; return 0; }
int main(void) { pthread_t t; if (pthread_create(&t, 0, worker, 0)) return 1; pthread_join(t, 0); return 0; }
