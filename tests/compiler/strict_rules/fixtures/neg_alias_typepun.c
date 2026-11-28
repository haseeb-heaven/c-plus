/* NEGATIVE alias/provenance: type-punned dereference through a wrong-type lvalue. */
#include <stdint.h>
int main(void) { float f = 3.14f; uint32_t bits = *(uint32_t *)&f; return bits != 0 ? 0 : 1; }
