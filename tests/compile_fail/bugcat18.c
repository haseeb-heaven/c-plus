/* Bug 446: handler and main both free the same shared pointer. */
#include <signal.h>
#include <stdlib.h>

static char *shared_ptr;

static void on_sig(int sig) {
  (void)sig;
  free(shared_ptr);
}

int main(void) {
  signal(SIGINT, on_sig);
  shared_ptr = (char *)malloc(32);
  free(shared_ptr);
  shared_ptr = NULL;
  return 0;
}
