/* Bug 427: buffered fprintf() from a handler before siglongjmp(). */
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>

static sigjmp_buf env;

static void on_sig(int sig) {
  (void)sig;
  fprintf(stderr, "signal %d\n", sig);
  siglongjmp(env, 1);
}

int main(void) {
  signal(SIGINT, on_sig);
  if (sigsetjmp(env, 1) == 0)
    raise(SIGINT);
  return 0;
}
