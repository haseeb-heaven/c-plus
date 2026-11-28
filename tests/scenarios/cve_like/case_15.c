/* Original user-supplied crash scenario 15 (minimally adjusted to compile). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
static uint64_t *active=NULL;
static void *worker(void *a){ (void)a; for(;;){ uint64_t *p=active; if(p) *p+=1; } return NULL; }
int main(void){ active=calloc(1024,sizeof(uint64_t)); pthread_t t; pthread_create(&t,NULL,worker,NULL);
 int cmd=0; printf("1 to reset: "); while(scanf("%d",&cmd)==1){ if(cmd==1&&active){ uint64_t *o=active; active=NULL; free(o);} }
 return 0; }
