#include <pthread.h>
#include <stdlib.h>
struct Config { int ready; int port; };
struct Config* global_cfg=NULL; pthread_mutex_t cfg_mtx=PTHREAD_MUTEX_INITIALIZER;
struct Config* get_config(void){ if(global_cfg==NULL){ pthread_mutex_lock(&cfg_mtx); if(global_cfg==NULL){ struct Config*tmp=(struct Config*)malloc(sizeof(struct Config)); tmp->port=8080; tmp->ready=1; global_cfg=tmp;} pthread_mutex_unlock(&cfg_mtx);} return global_cfg; }
int main(void){ struct Config*c=get_config(); free(c); return 0; }
