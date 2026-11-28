#include <pthread.h>
static void *worker(void *arg){ (void)arg; return 0; }
int main(void){ int local=7; pthread_t th; pthread_create(&th,0,worker,&local); pthread_join(th,0); return 0; }
