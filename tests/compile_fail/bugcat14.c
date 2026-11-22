/* Bug 409: handler frees shared state (re-entrant allocator from signal). */
#include <signal.h>
#include <stdlib.h>

static void *shared_state;

static void on_sig(int sig) {
  (void)sig;
  free(shared_state);
}

int main(void) {
  signal(SIGINT, on_sig);
  return 0;
}
