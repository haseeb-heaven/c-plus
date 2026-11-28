#include <pthread.h>
static void *worker(void *arg) { (void)arg; return NULL; }
int main(void) {
  int local = 1;
  pthread_t t;
  pthread_create(&t, NULL, worker, &local);
  pthread_join(t, NULL);
  return local;
}
