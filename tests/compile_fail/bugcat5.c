/* Bug 324: self-assignment via overlapping memcpy corrupts the owning
 * struct's handle bytes. */
#include <string.h>

struct Handle {
  int id;
};

void self_copy(struct Handle *h) { memcpy(h, h, sizeof *h); }

int main(void) { return 0; }
