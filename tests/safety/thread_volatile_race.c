#include <pthread.h>
static volatile int shared = 0;
static void *worker(void *arg) { (void)arg; for (int i=0;i<10;i++) shared++; return NULL; }
int main(void) { pthread_t t1,t2; pthread_create(&t1,0,worker,0); pthread_create(&t2,0,worker,0); pthread_join(t1,0); pthread_join(t2,0); return 0; }
