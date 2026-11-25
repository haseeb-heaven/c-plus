#include <pthread.h>
#include "cplus/cplus.h"
static int shared = 0;
resource struct Mutex { void *h; };
void Mutex_drop(Mutex *m) { if (m->h) { cplus_mutex_free(m->h); m->h = NULL; } }
Mutex Mutex_new(void) { Mutex m; m.h = cplus_mutex_new(); return m; }
resource struct Guard { Mutex *owner; };
void Guard_drop(Guard *g) {
  if (g->owner) { cplus_mutex_unlock(g->owner->h); g->owner = NULL; }
}
Guard Guard_lock(Mutex *m) { Guard g; g.owner = m; cplus_mutex_lock(m->h); return g; }
static void *worker(void *arg) {
  Mutex *m = (Mutex *)arg;
  for (int i = 0; i < 100; i++) {
    Guard g = Guard_lock(m);
    shared++;
    (void)&g;
  }
  return NULL;
}
int main(void) {
  Mutex m = Mutex_new();
  pthread_t t1, t2;
  pthread_create(&t1, NULL, worker, &m);
  pthread_create(&t2, NULL, worker, &m);
  pthread_join(t1, NULL);
  pthread_join(t2, NULL);
  return shared == 200 ? 0 : 1;
}
