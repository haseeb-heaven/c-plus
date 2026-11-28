/* POSITIVE alias/provenance: byte copy, no type-punned dereference. */
#include <stdint.h>
#include <string.h>
int main(void) { float f = 3.14f; uint32_t bits = 0; memcpy(&bits, &f, sizeof bits); return bits != 0 ? 0 : 1; }
