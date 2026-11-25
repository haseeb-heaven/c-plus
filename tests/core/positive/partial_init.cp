#include "cplus/cplus.h"
static int drops = 0;
resource struct Sock { int fd; };
void Sock_drop(Sock *s) { if (s->fd >= 0) { drops++; s->fd = -1; } }
int main(void) {
  { Sock s; } /* zero-initialized: fd == 0 is a valid live handle */
  return drops == 1 ? 0 : 1;
}
