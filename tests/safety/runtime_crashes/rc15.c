#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
static uint64_t *active=0;
static void *worker(void*a){(void)a; for(;;){uint64_t*p=active; if(p)*p+=1;} return 0;}
int main(void){active=calloc(1024,sizeof(uint64_t)); pthread_t t; pthread_create(&t,0,worker,0); int cmd=0; printf("cmd: "); while(scanf("%d",&cmd)==1){if(cmd==1&&active){uint64_t*old=active;active=0;free(old);}} return 0;}
