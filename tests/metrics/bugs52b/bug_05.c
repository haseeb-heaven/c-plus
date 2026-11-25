#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
void*cancellable_task(void*arg){(void)arg; void*scratch=malloc(1024); sleep(5); free(scratch); return NULL;}
int main(void){ pthread_t tid; pthread_create(&tid,NULL,cancellable_task,NULL); usleep(1000); pthread_cancel(tid); pthread_join(tid,NULL); return 0; }
