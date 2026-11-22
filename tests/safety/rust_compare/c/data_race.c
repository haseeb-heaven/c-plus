#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g;static void*t(void*a){(void)a;g++;return 0;}
int main(void){pthread_t a,b;pthread_create(&a,0,t,0);pthread_create(&b,0,t,0);pthread_join(a,0);pthread_join(b,0);return 0;}
