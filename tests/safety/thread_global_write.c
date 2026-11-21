#include <pthread.h>
static int shared = 0;
static void *worker(void *arg) { (void)arg; for (int i=0;i<10;i++) shared++; return NULL; }
int main(void) {
  pthread_t t1, t2;
  pthread_create(&t1, NULL, worker, NULL);
  pthread_create(&t2, NULL, worker, NULL);
  pthread_join(t1, NULL); pthread_join(t2, NULL);
  return shared;
}
