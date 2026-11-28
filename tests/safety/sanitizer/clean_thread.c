#include <pthread.h>
static int shared_counter;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void *increment(void *unused) {
  (void)unused;
  pthread_mutex_lock(&lock);
  shared_counter++;
  pthread_mutex_unlock(&lock);
  return NULL;
}
int main(void) {
  pthread_t a, b;
  if (pthread_create(&a, NULL, increment, NULL) != 0) return 2;
  if (pthread_create(&b, NULL, increment, NULL) != 0) return 2;
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  return shared_counter == 2 ? 0 : 1;
}
