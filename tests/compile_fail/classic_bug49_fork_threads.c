/* Bug 49: fork() after pthread_create can deadlock in the child. */
#include <pthread.h>
#include <unistd.h>
void *worker(void *p) { (void)p; return NULL; }
int main(void) { pthread_t t; pthread_create(&t, NULL, worker, NULL); pid_t p = fork(); (void)p; return 0; }
