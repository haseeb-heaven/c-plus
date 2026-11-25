#include <pthread.h>
static int shared_counter;
static void *increment(void *unused) {
  (void)unused;
  shared_counter++;
  return NULL;
}
int main(void) {
  pthread_t a, b;
  if (pthread_create(&a, NULL, increment, NULL) != 0) return 2;
  if (pthread_create(&b, NULL, increment, NULL) != 0) return 2;
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  return 0;
}
