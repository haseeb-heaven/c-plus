/* Bug 437: handler writes a 64-bit counter that is not sig_atomic_t. */
#include <signal.h>
#include <stdint.h>

static uint64_t ticker;

static void on_sig(int sig) {
  (void)sig;
  ticker = 0x1122334455667788ULL;
}

int main(void) {
  signal(SIGINT, on_sig);
  return (int)ticker;
}
