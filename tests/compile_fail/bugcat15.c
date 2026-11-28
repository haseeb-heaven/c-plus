/* Bug 417: re-entrant malloc() called from a signal handler. */
#include <signal.h>
#include <stdlib.h>

static void on_sig(int sig) {
  (void)sig;
  (void)malloc(64);
}

int main(void) {
  signal(SIGINT, on_sig);
  return 0;
}
