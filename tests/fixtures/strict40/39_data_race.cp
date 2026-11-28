#include <pthread.h>
static int shared_value = 0;
static void *worker(void *unused) { (void)unused; shared_value++; return 0; }
int main(void) { pthread_t first, second; if (pthread_create(&first, 0, worker, 0)) return 1; if (pthread_create(&second, 0, worker, 0)) return 1; pthread_join(first, 0); pthread_join(second, 0); return shared_value == 2 ? 0 : 1; }
