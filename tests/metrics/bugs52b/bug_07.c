#include <pthread.h>
#include <stdlib.h>
int* shared_ptr=NULL;
void*thread_a(void*arg){(void)arg; if(shared_ptr!=NULL){*shared_ptr=10;} return NULL;}
void*thread_b(void*arg){(void)arg; free(shared_ptr); shared_ptr=NULL; return NULL;}
int main(void){ shared_ptr=(int*)malloc(sizeof(int)); pthread_t t1,t2; pthread_create(&t1,NULL,thread_a,NULL); pthread_create(&t2,NULL,thread_b,NULL); pthread_join(t1,NULL); pthread_join(t2,NULL); return 0; }
