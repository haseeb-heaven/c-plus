/* Plain C translation unit used by test_shadow_metadata.c.
 *
 * It deliberately does NOT include the C+ runtime header: the pointer is
 * stored in an opaque struct and returned as an ordinary void *. This is the
 * ABI boundary that issue #182 requires to preserve bounds resolution when a
 * low-fat interior pointer round-trips through uninstrumented precompiled C.
 */
#include <stddef.h>
#include <stdint.h>

struct cplus_test_opaque {
  void *ptr;
  uintptr_t copy;
  unsigned marker;
};

void *cplus_test_roundtrip(void *p, void *opaque) {
  struct cplus_test_opaque *box = (struct cplus_test_opaque *)opaque;
  if (!box)
    return p;
  box->ptr = p;
  box->copy = (uintptr_t)p;
  box->marker = 0xc0ffeeu;
  return (void *)box->copy;
}
