#include <pthread.h>
#include <unistd.h>
void*worker(void*arg){ int*v=(int*)arg; usleep(10000); *v=42; return NULL; }
void spawn_job(void){ int local_status=0; pthread_t tid; pthread_create(&tid,NULL,worker,&local_status); pthread_detach(tid); }
int main(void){ spawn_job(); usleep(20000); return 0; }
