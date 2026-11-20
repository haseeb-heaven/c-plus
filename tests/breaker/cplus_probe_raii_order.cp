#include "cplus/cplus.h"
static int seq = 0;
resource struct Handle { int id; };
void Handle_drop(Handle *handle) { seq = seq*10 + handle->id; }
int main(void) {
  { Handle a={1}; Handle b={2}; Handle c={3}; }
  return seq == 321 ? 0 : 1;
}
